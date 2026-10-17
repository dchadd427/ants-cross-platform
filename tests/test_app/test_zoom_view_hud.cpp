// Zoom view, part 2: the HUD (the click centre, the cursor, the orders, the rubber band, the minimap and the scroll, the radar frame, the gating, Ctrl+N).
// (See test_zoom_view.cpp for what the whole program checks.)
#include "zoom_view_fixtures.hpp"

namespace {

// =====================================================================================================================================================
// HUD: a click goes to the tile that covers most of the pixel
// =====================================================================================================================================================

// Below 1 (and at 1.19, 1.41, 1.68) a screen pixel is not a whole world pixel and many pixels straddle a tile edge: the tile that a click picks there has to be the one that covers most of
// the pixel (a pixel's centre is in it), not the one under the pixel's left edge. The oracle is the coverage itself, in long doubles: the footprint of the screen pixel at `offset` is
// [origin + offset / z, origin + (offset + 1) / z); when an edge of a tile (a multiple of 32) lies inside it, the tile with the larger part wins; an exact tie says nothing.
// The model alone cannot say whether the picture agrees: SDL's software renderer places the last copy on whole pixels, up to a pixel off, which is as much as the difference between the rules.
struct Majority {
    int32_t tile{0};              // the tile that has most of the pixel
    int32_t left_tile{0};         // the tile under the pixel's left edge (the rule that was)
    bool straddles{false};        // an edge of a tile lies inside the pixel
    bool tie{false};
};

Majority majority_tile(double origin, float z, int32_t offset) {
    if (z == 1.0f) origin = std::floor(origin);                     // (the camera at the zoom 1 keeps its own arithmetic: the whole part of the origin; in play it is whole)
    const long double lo = static_cast<long double>(origin) + static_cast<long double>(offset) / static_cast<long double>(z);
    const long double hi = static_cast<long double>(origin) + static_cast<long double>(offset + 1) / static_cast<long double>(z);
    Majority m;
    m.left_tile = static_cast<int32_t>(std::floor(lo / 32.0L));
    const int32_t last = static_cast<int32_t>(std::floor((hi - 1e-9L) / 32.0L));
    m.tile = m.left_tile;
    if (last > m.left_tile) {                                       // (a pixel is never wider than a tile at the levels of the series)
        const long double edge = static_cast<long double>(last) * 32.0L;
        const long double before = edge - lo;
        const long double after = hi - edge;
        m.straddles = true;
        m.tie = std::fabs(static_cast<double>(before - after)) < 1e-6;
        m.tile = before > after ? m.left_tile : last;
    }
    return m;
}

}  // namespace

void test_click_centre(const assets::AssetArchive& arc) {
    group("centre", "a click goes to the tile that covers most of the pixel, at every level, in both pictures: every pixel column and row of the view");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            long straddling = 0;
            long differing = 0;                                     // the straddling pixels where the left edge's tile is not the majority's: what the rule changed
            for (const auto& cam : {std::pair<double, double>{0.0, 0.0}, std::pair<double, double>{700.25, 300.5}, std::pair<double, double>{1.0e9, 1.0e9}}) {
                rig.look(cam.first, cam.second, z);
                const double ox = static_cast<double>(rig.camera.x);
                const double oy = static_cast<double>(rig.camera.y);
                // a row and a column away from the ants of the HUD's world (tile rows 3, 30, 55; columns 3, 28, 33, 55), so that every click is a move order
                const auto clear_of = [](const Majority& m, std::initializer_list<int32_t> ants) {
                    bool ok = !m.straddles;
                    for (const int32_t a : ants) ok = ok && std::abs(m.tile - a) > 1;
                    return ok;
                };
                int32_t row = -1;
                for (int32_t y = view.y + 10; y < view.bottom() - 10 && row < 0; ++y) {
                    if (clear_of(majority_tile(oy, z, y - view.y), {3, 30, 55})) row = y;
                }
                int32_t col = -1;
                for (int32_t x = view.x + 10; x < view.right() - 10 && col < 0; ++x) {
                    if (clear_of(majority_tile(ox, z, x - view.x), {3, 28, 33, 55})) col = x;
                }
                check(row >= 0 && col >= 0, at + "a row and a column away from the ants were found");
                if (row < 0 || col < 0) continue;
                int wrong = 0;
                for (int32_t x = view.x; x < view.right(); ++x) {
                    const Majority m = majority_tile(ox, z, x - view.x);
                    rig.hud.select_ant(rig.mine);
                    rig.sink.commands.clear();
                    rig.click(x, row);
                    if (rig.sink.commands.size() != 1 || rig.sink.commands[0].type != sim::CommandType::GroupMove) {
                        ++wrong;
                        continue;
                    }
                    straddling += m.straddles ? 1 : 0;
                    differing += m.straddles && !m.tie && m.tile != m.left_tile ? 1 : 0;
                    if (!m.tie && rig.sink.commands[0].tile_x != m.tile) ++wrong;
                }
                for (int32_t y = view.y; y < view.bottom(); ++y) {
                    const Majority m = majority_tile(oy, z, y - view.y);
                    rig.hud.select_ant(rig.mine);
                    rig.sink.commands.clear();
                    rig.click(col, y);
                    if (rig.sink.commands.size() != 1 || rig.sink.commands[0].type != sim::CommandType::GroupMove) {
                        ++wrong;
                        continue;
                    }
                    straddling += m.straddles ? 1 : 0;
                    differing += m.straddles && !m.tie && m.tile != m.left_tile ? 1 : 0;
                    if (!m.tie && rig.sink.commands[0].tile_y != m.tile) ++wrong;
                }
                check(wrong == 0, at + "from (" + num(cam.first) + ", " + num(cam.second) + ") every pixel column and row goes to the tile with most of the pixel: " + std::to_string(wrong) + " wrong");
            }
            if (z == 1.0f || z == 2.0f || z == 0.5f || z == 0.25f) check(straddling == 0, at + "an exact level has no pixel that straddles a tile edge");
            else check(straddling >= 30 && differing >= 10, at + "the sweep has pixels that straddle an edge, and in some of them the left edge's tile is not the one that covers most: " + std::to_string(straddling) + ", " + std::to_string(differing));
        }
    }
}

// =====================================================================================================================================================
// HUD: the pointer, the orders, the rubber band, the minimap, the edge scroll and the radar's frame at a zoom
// =====================================================================================================================================================

void test_hud_cursor(const assets::AssetArchive& arc) {
    group("cursor", "the cursor under the pointer follows the world at every zoom, pixel for pixel");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            rig.hud.select_ant(rig.mine);
            const sim::AntSnapshot& foe = rig.ant(rig.foe);
            const sim::AntSnapshot& mine = rig.ant(rig.mine);
            rig.camera.zoom = z;
            rig.camera.center_on(foe.px - 80, foe.py, 60, 60);
            // the sweep: the foe's hit box and its surroundings, and the own worker's
            int attack = 0;
            int select = 0;
            int move = 0;
            int wrong = 0;
            for (int32_t sy = view.y + 10; sy < view.bottom() - 10; sy += 1) {
                for (int32_t sx = view.x + 10; sx < view.right() - 10; sx += (wide ? 2 : 1)) {
                    const int32_t wx = world_under_f(rig.camera.x, z, sx - view.x);
                    const int32_t wy = world_under_f(rig.camera.y, z, sy - view.y);
                    bool in_foe = false;                                                     // (every ant of the world: a level that shows the whole map has the far ants in view too)
                    bool in_mine = false;
                    for (const auto& a : rig.world().ants) {
                        const bool in = wx >= a.px - 20 && wx < a.px + 20 && wy >= a.py - 32 && wy < a.py + 16;
                        if (a.player_id == mine.player_id) in_mine = in_mine || in;
                        else in_foe = in_foe || in;
                    }
                    const CursorType want = in_foe ? CursorType::Attack : in_mine ? CursorType::Select : CursorType::Move;
                    const CursorType got = rig.hud.evaluate_cursor(sx, sy, rig.world(), rig.sim.grid(), rig.camera);
                    if (got != want && wrong == 0) std::fprintf(stderr, "  (first wrong cursor: screen (%d, %d) world (%d, %d) got %d want %d, camera (%g, %g) zoom %g, foe at (%d, %d), mine at (%d, %d))\n", sx, sy, wx, wy, static_cast<int>(got), static_cast<int>(want),
                                                                static_cast<double>(rig.camera.x), static_cast<double>(rig.camera.y), static_cast<double>(z), foe.px, foe.py, mine.px, mine.py);
                    if (got != want) ++wrong;
                    attack += got == CursorType::Attack;
                    select += got == CursorType::Select;
                    move += got == CursorType::Move;
                }
            }
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            check(wrong == 0, at + "every pixel's cursor is the one of the world under it: " + std::to_string(wrong) + " wrong");
            const double box = 40.0 * 48.0 * static_cast<double>(z) * static_cast<double>(z) * 0.7 / (wide ? 2.0 : 1.0);           // (most of a hit box's pixels: the sweep's step is 2 across in the wide view)
            check(attack > box && select > box && move > 1000, at + "the sweep meets the attack, select and move cursors (" + std::to_string(attack) + ", " + std::to_string(select) + ", " + std::to_string(move) + ")");
            // the hit box in screen pixels is 40 x 48 world pixels times the zoom
            if (z == 0.5f || z == 1.0f || z == 2.0f) check(attack == static_cast<int>(std::lround(40.0 * z)) * static_cast<int>(std::lround(48.0 * z)) / (wide ? 2 : 1) || wide, at + "the attack area is the foe's hit box scaled by the zoom: " + std::to_string(attack));
            else check(wide || std::fabs(attack - 40.0 * 48.0 * static_cast<double>(z) * z) <= (40.0 + 48.0) * z + 2.0, at + "the attack area is the foe's hit box scaled by the zoom, to a pixel's edge: " + std::to_string(attack));
        }
    }
}

void test_hud_orders(const assets::AssetArchive& arc) {
    group("orders", "a click orders the tile under it, also at the first and the last pixel of the view; the click marker is at the world point");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            struct Spot {
                double ox, oy;
                int32_t sx, sy;
                const char* what;
            };
            std::vector<Spot> spots;
            // the corners and the middle of the view from a camera in the middle of the map, then the corners of the map from the cameras that sit in them
            for (const auto& p : {std::pair<int32_t, int32_t>{view.x, view.y}, std::pair<int32_t, int32_t>{view.right() - 1, view.y}, std::pair<int32_t, int32_t>{view.x, view.bottom() - 1},
                                  std::pair<int32_t, int32_t>{view.right() - 1, view.bottom() - 1}, std::pair<int32_t, int32_t>{view.x + view.w / 2, view.y + view.h / 2}}) {
                spots.push_back(Spot{600.0, 700.0, p.first, p.second, "the view's edge pixels"});
                spots.push_back(Spot{0.0, 0.0, p.first, p.second, "from the map's top left corner"});
                spots.push_back(Spot{1.0e9, 1.0e9, p.first, p.second, "from the map's bottom right corner"});
            }
            for (int32_t sy = view.y + 3; sy < view.bottom(); sy += 61) {
                for (int32_t sx = view.x + 5; sx < view.right(); sx += 67) spots.push_back(Spot{700.25, 650.5, sx, sy, "a grid of pixels"});
            }
            int checked = 0;
            for (const Spot& sp : spots) {
                rig.look(sp.ox, sp.oy, z);
                rig.hud.select_ant(rig.mine);
                rig.sink.commands.clear();
                rig.markers.clear();
                const int32_t wx = world_under_f(rig.camera.x, z, sp.sx - view.x);
                const int32_t wy = world_under_f(rig.camera.y, z, sp.sy - view.y);
                // (a spot that has an ant under it is a select or an attack, not an order: the far ants and the two near ones are away from every spot of these cameras, except the corner
                // cameras: the ants of the corners are skipped)
                bool ant_there = false;
                for (const auto& a : rig.world().ants) {
                    ant_there = ant_there || (wx >= a.px - 20 - 32 && wx < a.px + 20 + 32 && wy >= a.py - 32 - 32 && wy < a.py + 16 + 32);
                }
                if (ant_there) continue;
                rig.click(sp.sx, sp.sy);
                ++checked;
                const std::string where = at + sp.what + " (" + std::to_string(sp.sx) + ", " + std::to_string(sp.sy) + ")";
                check(rig.sink.commands.size() == 1 && rig.sink.commands[0].type == sim::CommandType::GroupMove, where + ": one move order");
                if (rig.sink.commands.size() == 1) check(rig.sink.commands[0].tile_x == wx / 32 && rig.sink.commands[0].tile_y == wy / 32, where + ": to the tile of the world pixel under it: (" + std::to_string(wx / 32) + ", " + std::to_string(wy / 32) + ")");
                check(rig.markers.size() == 1 && rig.markers[0].first == wx && rig.markers[0].second == wy, where + ": the click marker is at the world pixel (" + std::to_string(wx) + ", " + std::to_string(wy) + ")");
                // the right button does the same (it acts at its release)
                rig.sink.commands.clear();
                rig.markers.clear();
                rig.click(sp.sx, sp.sy, SDL_BUTTON_RIGHT);
                check(rig.sink.commands.size() == 1 && rig.sink.commands[0].tile_x == wx / 32 && rig.sink.commands[0].tile_y == wy / 32, where + ": the right button orders the same tile");
                check(rig.markers.size() == 1 && rig.markers[0].first == wx && rig.markers[0].second == wy, where + ": and marks the same world pixel");
            }
            check(checked > 30, at + "the clicks that were made: " + std::to_string(checked));
            // an order at the foe: an attack at its tile (the pointer at an edge of its hit box)
            rig.look(0.0, 0.0, z);
            rig.camera.center_on(rig.ant(rig.foe).px, rig.ant(rig.foe).py, 60, 60);
            for (const uint8_t button : {static_cast<uint8_t>(SDL_BUTTON_LEFT), static_cast<uint8_t>(SDL_BUTTON_RIGHT)}) {
                const char* const which = button == SDL_BUTTON_LEFT ? "left" : "right";
                int attacks = 0;
                int expected = 0;
                for (int32_t sy = view.y + view.h / 2 - 60; sy < view.y + view.h / 2 + 60; ++sy) {
                    for (int32_t sx = view.x + view.w / 2 - 60; sx < view.x + view.w / 2 + 60; ++sx) {
                        const int32_t wx = world_under_f(rig.camera.x, z, sx - view.x);
                        const int32_t wy = world_under_f(rig.camera.y, z, sy - view.y);
                        const auto& foe = rig.ant(rig.foe);
                        const bool in = wx >= foe.px - 20 && wx < foe.px + 20 && wy >= foe.py - 32 && wy < foe.py + 16;
                        rig.hud.select_ant(rig.mine);
                        rig.sink.commands.clear();
                        rig.click(sx, sy, button);
                        const bool attacked = rig.sink.commands.size() == 1 && rig.sink.commands[0].type == sim::CommandType::GroupAttack;
                        attacks += attacked;
                        expected += in;
                        if (attacked != in) check(false, at + "an attack with the " + which + " button at (" + std::to_string(sx) + ", " + std::to_string(sy) + ") is the foe's hit box under the pointer");
                    }
                }
                check(attacks == expected && attacks > 40.0 * 48.0 * static_cast<double>(z) * static_cast<double>(z) * 0.7, at + "the " + which + " button orders an attack exactly where the foe's hit box is: " + std::to_string(attacks) + " of " + std::to_string(expected));
            }
        }
    }
}

void test_hud_rubber_band(const assets::AssetArchive& arc) {
    group("band", "the rubber band selects the ants whose hit box overlaps the world that it covers, at every zoom");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            const sim::AntSnapshot& target = rig.ant(rig.mine);
            rig.camera.zoom = z;
            rig.camera.center_on(target.px, target.py, 60, 60);
            // the screen position of the ant's hit box: left edge world px - 20, right px + 20, top py - 32, bottom py + 16
            auto expected_selected = [&](int32_t left, int32_t top, int32_t right, int32_t bottom) {
                const int32_t wl = world_under_f(rig.camera.x, z, left - view.x);
                const int32_t wt = world_under_f(rig.camera.y, z, top - view.y);
                const int32_t wr = world_under_f(rig.camera.x, z, right - view.x);
                const int32_t wb = world_under_f(rig.camera.y, z, bottom - view.y);
                return std::max(wl, target.px - 20) < std::min(wr, target.px + 20) && std::max(wt, target.py - 32) < std::min(wb, target.py + 16);
            };
            int selected = 0;
            int wrong = 0;
            int tested = 0;
            // bands from a fixed corner far from the ant to the corner that moves across the ant's box on every side (and bands that start inside the box)
            struct Band {
                int32_t sx, sy;
            };
            const int32_t cx = view.x + view.w / 2;
            const int32_t cy = view.y + view.h / 2;
            const int32_t reach = static_cast<int32_t>(std::lround(60.0 * z)) + 8;
            for (const Band b : {Band{view.x + 12, view.y + 12}, Band{view.right() - 14, view.bottom() - 14}, Band{view.right() - 14, view.y + 12}, Band{view.x + 12, view.bottom() - 14}, Band{cx, cy}}) {
                for (int32_t ex = cx - reach; ex <= cx + reach; ex += 1) {
                    for (int32_t ey : {cy - reach, cy - reach / 2, cy, cy + reach / 2, cy + reach}) {
                        rig.hud.clear_selection();
                        rig.hud.handle_mouse_down(b.sx, b.sy, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                        rig.hud.handle_mouse_motion(ex, ey, rig.sim, rig.camera);
                        rig.hud.handle_mouse_up(ex, ey, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                        // the band: the pointer kept 1 px inside the view, a stationary press (no extent on an axis) a 2 x 2 dot
                        int32_t left = std::min(b.sx, std::clamp(ex, view.x + 1, view.right() - 1));
                        int32_t right = std::max(b.sx, std::clamp(ex, view.x + 1, view.right() - 1));
                        int32_t top = std::min(b.sy, std::clamp(ey, view.y + 1, view.bottom() - 1));
                        int32_t bottom = std::max(b.sy, std::clamp(ey, view.y + 1, view.bottom() - 1));
                        if (right == left) { --left; ++right; }
                        if (bottom == top) { --top; ++bottom; }
                        // (a band of 4 pixels or less in both directions is a click, not a band: skip those, the click is checked above)
                        if (right - left <= 4 && bottom - top <= 4) continue;
                        const bool want = expected_selected(left, top, right, bottom);
                        bool got = false;
                        for (uint32_t id : rig.hud.get_selected_ant_ids()) got = got || id == rig.mine;
                        ++tested;
                        selected += got;
                        if (got != want) ++wrong;
                    }
                }
            }
            check(wrong == 0, at + "the band selects the ant exactly when its world rectangle overlaps the hit box: " + std::to_string(wrong) + " wrong of " + std::to_string(tested));
            check(selected > 50 && selected < tested - 50, at + "the bands sweep across the box's edges (" + std::to_string(selected) + " of " + std::to_string(tested) + ")");
        }
    }
}

void test_hud_minimap_and_scroll(const assets::AssetArchive& arc) {
    group("scroll", "the minimap centres the world that is seen; the edge scroll moves the same distance on the screen; the arrows show where the view can move");
    constexpr uint32_t kTiles = 125;                                  // a map of 4000 px: the view is far from its edges at every zoom, so no clamp bends the distances
    constexpr double kMapPx = 4000.0;
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide, kTiles);
        const LayoutRect view = rig.layout.view();
        const LayoutRect mini = rig.layout.minimap();
        // 1. the minimap: a press, the 50 ms ticks, the middle of the world that is seen is the point under the pointer
        for (const float z : zooms_for(wide, kTiles)) {
            if (z < 0.25f) continue;                                          // (the world seen at a smaller zoom is most of this 4000 px map: the model's own test (test_zoom_model) has the tiny levels on a bigger one)
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            int tested = 0;
            int centred = 0;
            for (int32_t my = mini.y + 3; my < mini.bottom(); my += 4) {
                for (int32_t mx = mini.x + 3; mx < mini.right(); mx += 4) {
                    rig.look(0.0, 0.0, z);
                    rig.hud.handle_mouse_down(mx, my, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                    for (int i = 0; i < 4; ++i) rig.hud.input_tick(rig.camera, kTiles, kTiles, mx, my);
                    rig.hud.handle_mouse_up(mx, my, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                    const double want_x = std::trunc((mx - mini.x) * (kMapPx / mini.w));
                    const double want_y = std::trunc((my - mini.y) * (kMapPx / mini.h));
                    const double half_w = view.w / static_cast<double>(z) / 2.0;
                    const double half_h = view.h / static_cast<double>(z) / 2.0;
                    if (want_x >= half_w && want_x <= kMapPx - half_w && want_y >= half_h && want_y <= kMapPx - half_h) {
                        ++tested;
                        const double cxw = rig.camera.x + half_w;
                        const double cyw = rig.camera.y + half_h;
                        if (std::fabs(cxw - want_x) <= 2.0 * zoom::grid(z) + 1e-9 && std::fabs(cyw - want_y) <= 2.0 * zoom::grid(z) + 1e-9) ++centred;           // (the minimap works in whole screen pixels: two of them)
                    }
                }
            }
            check(tested > (z < 0.5f ? 20 : 100) && centred == tested, at + "a press on the minimap centres the world that is seen on the point under the pointer: " + std::to_string(centred) + " of " + std::to_string(tested));
            // a right click on the minimap orders the point's tile, whatever the zoom
            rig.look(600.0, 600.0, z);
            rig.hud.select_ant(rig.mine);
            rig.sink.commands.clear();
            rig.click(mini.x + 50, mini.y + 40, SDL_BUTTON_RIGHT);
            check(rig.sink.commands.size() == 1 && rig.sink.commands[0].tile_x == static_cast<int16_t>(std::trunc(50.0 * (kMapPx / mini.w)) / 32) && rig.sink.commands[0].tile_y == static_cast<int16_t>(std::trunc(40.0 * (kMapPx / mini.h)) / 32),
                  at + "a right click on the minimap orders the tile of the minimap's point, the same at every zoom");
        }

        // 2. the edge scroll: the distance on the screen is the zoom 1 distance, from a camera far from the map's edges
        const int32_t pw = rig.layout.width;
        const int32_t ph = rig.layout.height;
        std::vector<std::pair<int32_t, int32_t>> ring;
        for (int32_t x = 0; x < pw; x += (x < 30 || x > pw - 30) ? 1 : 7) {
            for (int32_t y : {0, 1, 2, 3, 4, 6, 9, 11, ph - 12, ph - 10, ph - 7, ph - 5, ph - 4, ph - 3, ph - 2, ph - 1}) ring.emplace_back(x, y);
        }
        for (int32_t y = 0; y < ph; y += (y < 30 || y > ph - 30) ? 1 : 5) {
            for (int32_t x : {0, 1, 2, 3, 4, 6, 9, 11, pw - 12, pw - 10, pw - 7, pw - 5, pw - 4, pw - 3, pw - 2, pw - 1}) ring.emplace_back(x, y);
        }
        for (int32_t rate : {0, 50, 99}) {
            for (const float z : zooms_for(wide, kTiles)) {
                if (z < 0.4f) continue;                                       // (a step of up to 109 screen pixels from (700, 700) must not reach the far edge of the 4000 px map)
                const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ", rate " + std::to_string(rate) + ": ";
                int differences = 0;
                int moved = 0;
                for (const auto& p : ring) {
                    // (the options' scroll rate is the HUD's setting: an options state of its own is not needed, the default is 50 and `rate` is only used by the model; the test sets it
                    // through the stored option)
                    ConfigStore store;
                    store.set_int("Scroll Speed", rate);
                    rig.hud.options().load(store);
                    HudRig& r = rig;
                    r.look(700.0, 700.0, 1.0f);
                    const float bx = r.camera.x;
                    const float by = r.camera.y;
                    r.hud.input_tick(r.camera, kTiles, kTiles, p.first, p.second);
                    const double base_dx = r.camera.x - bx;
                    const double base_dy = r.camera.y - by;
                    r.look(700.0, 700.0, z);
                    const float zx = r.camera.x;
                    const float zy = r.camera.y;
                    r.hud.input_tick(r.camera, kTiles, kTiles, p.first, p.second);
                    const double dx = (r.camera.x - zx) * z;
                    const double dy = (r.camera.y - zy) * z;
                    if (std::fabs(dx - base_dx) > 3e-3 || std::fabs(dy - base_dy) > 3e-3) ++differences;                 // (the origin is a float: 13000 world pixels are a thousandth of a pixel)
                    if (base_dx != 0.0 || base_dy != 0.0) ++moved;
                }
                check(differences == 0, at + "the distance on the screen is the zoom 1 distance at every pixel of the picture's edge: " + std::to_string(differences) + " of " + std::to_string(ring.size()) + " differ");
                check(moved > 200, at + "the strips scroll (" + std::to_string(moved) + " pixels)");
            }
        }

        // 3. the scroll arrows show where the view can move (at the zoom the world that is seen is view / zoom)
        {
            const CursorType kArrows[8] = {CursorType::ScrollN, CursorType::ScrollNE, CursorType::ScrollE, CursorType::ScrollSE, CursorType::ScrollS, CursorType::ScrollSW, CursorType::ScrollW, CursorType::ScrollNW};
            const std::pair<int32_t, int32_t> at_dir[8] = {{pw / 2, 4}, {pw - 4, 4}, {pw - 4, ph / 2}, {pw - 4, ph - 4}, {pw / 2, ph - 4}, {4, ph - 4}, {4, ph / 2}, {4, 4}};
            for (const float z : zooms_for(wide, kTiles)) {
                // the origin in whole screen pixels and the map in whole screen pixels at the zoom: the view can move while it has a pixel of the map left on that side
                const double map_screen = std::floor(kMapPx * static_cast<double>(z) + 1e-9);
                for (const auto& cam_at : {std::pair<double, double>{0.0, 0.0}, std::pair<double, double>{1.0e9, 1.0e9}, std::pair<double, double>{0.0, 1.0e9}, std::pair<double, double>{1.0e9, 0.0}, std::pair<double, double>{500.0, 500.0}}) {
                    rig.look(cam_at.first, cam_at.second, z);
                    const double ox = rig.camera.x;
                    const double oy = rig.camera.y;
                    const double ox_s = std::round(ox * static_cast<double>(z));
                    const double oy_s = std::round(oy * static_cast<double>(z));
                    const bool west = ox_s > 0.0;
                    const bool east = ox_s + view.w < map_screen;
                    const bool north = oy_s > 0.0;
                    const bool south = oy_s + view.h < map_screen;
                    const bool can[8] = {north, east || north, east, east || south, south, west || south, west, west || north};
                    for (int d = 0; d < 8; ++d) {
                        rig.hud.clear_selection();
                        const CursorType got = rig.hud.evaluate_cursor(at_dir[d].first, at_dir[d].second, rig.world(), rig.sim.grid(), rig.camera);
                        check((got == kArrows[d]) == can[d], std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + " from the origin (" + num(ox) + ", " + num(oy) + "): the arrow " + std::to_string(d) +
                                                                  " shows exactly when the view can move that way (" + (can[d] ? "can" : "cannot") + ")");
                    }
                }
            }
        }
    }
}

void test_hud_radar_frame(const assets::AssetArchive& arc) {
    group("radar", "the minimap's frame is the world that is seen at the zoom");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        const LayoutRect mini = rig.layout.minimap();
        for (const float z : zooms_for(wide)) {
            for (const auto& cam_at : {std::pair<double, double>{0.0, 0.0}, std::pair<double, double>{333.0, 479.0}, std::pair<double, double>{1.0e9, 1.0e9}, std::pair<double, double>{500.5, 700.5}}) {
                rig.look(cam_at.first, cam_at.second, z);
                RectRenderer rec;
                rig.hud.render(rec, rig.arc, rig.world(), rig.camera);
                const RectRenderer::Rect* frame = nullptr;
                for (const auto& r : rec.frames) {
                    if (r.color.r == 251 && r.color.g == 251 && r.color.b == 255) frame = &r;
                }
                const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + " from (" + num(rig.camera.x) + ", " + num(rig.camera.y) + "): ";
                check(frame != nullptr, at + "the HUD draws the minimap's frame");
                if (frame == nullptr) continue;
                // the frame: the seen world scaled to the image, plus one; at the image's far edges it is moved inside; the origin scaled down
                const int64_t world = 1920;
                const int32_t seen_w = seen_f(view.w, z);
                const int32_t seen_h = seen_f(view.h, z);
                const int32_t frame_w = static_cast<int32_t>(seen_w * static_cast<int64_t>(mini.w) / world) + 1;
                const int32_t frame_h = static_cast<int32_t>(seen_h * static_cast<int64_t>(mini.h) / world) + 1;
                int32_t fl = mini.x + static_cast<int32_t>(static_cast<int32_t>(rig.camera.x) * static_cast<int64_t>(mini.w) / world);
                int32_t ft = mini.y + static_cast<int32_t>(static_cast<int32_t>(rig.camera.y) * static_cast<int64_t>(mini.h) / world);
                int32_t fr = fl + frame_w;
                int32_t fb = ft + frame_h;
                if (fr >= mini.x + mini.w) { fr = mini.x + mini.w; fl = fr - frame_w; }
                if (fb >= mini.y + mini.h) { fb = mini.y + mini.h; ft = fb - frame_h; }
                if (seen_w >= world) { fl = mini.x; fr = mini.x + mini.w; }                // the view shows the map's whole width (the zoom-out's limit): the frame is the image's whole width
                if (seen_h >= world) { ft = mini.y; fb = mini.y + mini.h; }
                check(frame->x == fl && frame->y == ft && frame->w == fr - fl && frame->h == fb - ft,
                      at + "the frame is (" + std::to_string(fl) + ", " + std::to_string(ft) + ", " + std::to_string(fr - fl) + " x " + std::to_string(fb - ft) + "), the HUD drew (" + std::to_string(frame->x) + ", " + std::to_string(frame->y) + ", " +
                          std::to_string(frame->w) + " x " + std::to_string(frame->h) + ")");
            }
        }
        // a map that the view shows whole on an axis: the frame is the image's whole width / height there
        {
            sim::SimulationEngine small;
            small.init_test_world(12, 12, 1, 600000);
            HUD hud;
            hud.init(0);
            hud.set_layout(rig.layout);
            ViewportCamera cam;
            cam.set_view(view);
            cam.centre_small_maps = wide;
            for (const float z : kZooms) {
                cam.zoom = z;
                cam.set_origin(0.0, 0.0, 12, 12);
                RectRenderer rec;
                hud.render(rec, rig.arc, small.get_world_state(), cam);
                const RectRenderer::Rect* frame = nullptr;
                for (const auto& r : rec.frames) {
                    if (r.color.r == 251 && r.color.g == 251 && r.color.b == 255) frame = &r;
                }
                const bool whole = z <= 1.0f;                              // (a 384 px map is whole in 442 x 440 and 762 x 500, not in the 221 x 220 or 381 x 250 of the zoom 2)
                check(frame != nullptr && (whole ? (frame->x == mini.x && frame->y == mini.y && frame->w == mini.w && frame->h == mini.h) : (frame->w <= mini.w && frame->h < mini.h)),
                      std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": a 12 x 12 map that the view shows whole has the whole image as its frame, one that it does not has a part");
            }
        }
    }
}

void test_hud_gating(const assets::AssetArchive& arc) {
    group("gating", "the wheel and the middle button may zoom only when nothing holds the mouse and no dialog is open");
    HudRig rig(arc, true);
    check(rig.hud.view_zoom_allowed(), "a HUD with nothing open allows the zoom");
    {
        HudRig r(arc, true);
        r.hud.open_options();
        check(!r.hud.view_zoom_allowed(), "not while the options window is open");
    }
    {
        HudRig r(arc, true);
        r.hud.open_quick_help();
        check(!r.hud.view_zoom_allowed(), "not while the quick help is open");
    }
    {
        HudRig r(arc, true);
        r.hud.open_quit_dialog();
        check(!r.hud.view_zoom_allowed(), "not while the quit dialog is open");
    }
    {
        HudRig r(arc, true);
        r.hud.start_match_modal();
        check(!r.hud.view_zoom_allowed(), "not while the \"get ready\" dialog is open");
        r.hud.dismiss_match_start_modal();
        check(r.hud.view_zoom_allowed(), "... and again after it");
    }
    {
        HudRig r(arc, true);
        r.sim.form_alliance(0, 2);
        r.sim.tick();
        r.hud.request_team_up(r.sim, 1);                              // with an ally the offer asks first: "Doing this will break your team ..."
        check(r.hud.alliance_dialog() == HUD::AllianceDialog::BreakConfirm && !r.hud.view_zoom_allowed(), "not while an alliance dialog is open");
        r.hud.handle_key_down('n', r.sim, r.camera);                  // No
        check(r.hud.alliance_dialog() == HUD::AllianceDialog::None && r.hud.view_zoom_allowed(), "... and again after it is answered");
    }
    {   // a press on the map is a rubber band; on the minimap a drag; on a button a captured button; the right button on the map a capture; the chat log a drag
        HudRig r(arc, true);
        const LayoutRect view = r.layout.view();
        const LayoutRect mini = r.layout.minimap();
        r.hud.handle_mouse_down(view.x + 100, view.y + 100, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not during a rubber band");
        r.hud.handle_mouse_up(view.x + 100, view.y + 100, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
        r.hud.handle_mouse_down(mini.x + 20, mini.y + 20, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while the minimap is held");
        r.hud.handle_mouse_up(mini.x + 20, mini.y + 20, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
        r.hud.handle_mouse_down(view.x + 100, view.y + 100, SDL_BUTTON_RIGHT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while the right button is held on the map");
        r.hud.handle_mouse_up(view.x + 100, view.y + 100, SDL_BUTTON_RIGHT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
        const UIButton help = r.hud.help_button();
        r.hud.handle_mouse_down(help.x + 3, help.y + 3, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while a button is pressed");
        r.hud.handle_mouse_up(help.x + 3, help.y + 3, SDL_BUTTON_LEFT, r.sim, r.camera);
        r.hud.close_quick_help();
        check(r.hud.view_zoom_allowed(), "... but after it (the quick help that the click opened is closed)");
        r.hud.handle_mouse_down(r.layout.chat_view().x + 10, r.layout.chat_view().y + 10, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while the chat log is dragged");
        r.hud.handle_mouse_up(r.layout.chat_view().x + 10, r.layout.chat_view().y + 10, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
    }
}

void test_hud_ctrl_n(const assets::AssetArchive& arc) {
    group("ctrln", "Ctrl+N and Ctrl+P scroll just far enough to show the ant's square in the world that the view shows at the zoom");
    struct Origin {
        double x, y;
        bool half_pixel;                         // (the origin of the zoom 2 may lie half a world pixel inside a world pixel)
    };
    const Origin origins[] = {{1000.0, 1000.0, false}, {0.0, 0.0, false}, {1000.5, 1000.5, true}, {0.5, 100.5, true}, {1500.0, 40.0, false}};
    int moved_right = 0;
    int moved_down = 0;
    for (const bool wide : {false, true}) {
        for (const float z : zooms_for(wide)) {
            for (const Origin& org : origins) {
                if (org.half_pixel && z != 2.0f) continue;
                for (const char key : {'n', 'p'}) {
                    HudRig rig(arc, wide);
                    const LayoutRect view = rig.layout.view();
                    rig.look(org.x, org.y, z);
                    rig.hud.clear_selection();
                    const double ox = rig.camera.x;
                    const double oy = rig.camera.y;
                    // the ant that the key picks (the search starts at the last own ant with nothing selected: 'n' takes the first of the player's ants, 'p' the one before the last)
                    std::vector<sim::AntSnapshot> own;
                    for (const auto& a : rig.world().ants) {
                        if (a.player_id == 0) own.push_back(a);
                    }
                    const sim::AntSnapshot chosen = key == 'n' ? own.front() : own[own.size() - 2];
                    rig.hud.handle_key_down(key, rig.sim, rig.camera, KMOD_CTRL);
                    const int32_t vis_w = seen_f(view.w, z);
                    const int32_t vis_h = seen_f(view.h, z);
                    const int32_t l = std::max(chosen.px - 128, 0);
                    const int32_t t = std::max(chosen.py - 128, 0);
                    const int32_t r = std::min(chosen.px + 128, 1920);
                    const int32_t b = std::min(chosen.py + 128, 1920);
                    const int32_t oix = static_cast<int32_t>(std::floor(ox));
                    const int32_t oiy = static_cast<int32_t>(std::floor(oy));
                    int32_t dx = 0;
                    int32_t dy = 0;
                    if (r > oix + vis_w) dx = r - (oix + vis_w);
                    else if (l < oix) dx = l - oix;
                    if (t < oiy) dy = t - oiy;
                    else if (b > oiy + vis_h) dy = b - (oiy + vis_h);
                    moved_right += dx > 0 ? 1 : 0;
                    moved_down += dy > 0 ? 1 : 0;
                    // (the origin goes to the lattice of the zoom and is held in [0, the map less the world that is seen], whose far end is on the lattice too)
                    const auto held = [z](double v, int32_t len) {
                        const double zz = static_cast<double>(z);
                        return std::clamp(std::floor(v * zz + 0.5) / zz, 0.0, std::floor((1920.0 - len / zz) * zz + 1e-9) / zz);
                    };
                    const double want_x = held(ox + dx, view.w);
                    const double want_y = held(oy + dy, view.h);
                    const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + " from (" + num(org.x) + ", " + num(org.y) + ") with Ctrl+" + static_cast<char>(key - 32) + ": ";
                    check(std::fabs(rig.camera.x - static_cast<float>(want_x)) < 1e-3f && std::fabs(rig.camera.y - static_cast<float>(want_y)) < 1e-3f,
                          at + "the view moves from (" + num(ox) + ", " + num(oy) + ") to (" + num(want_x) + ", " + num(want_y) + "), it went to (" + num(rig.camera.x) + ", " + num(rig.camera.y) + ")");
                    check(rig.camera.world_x == static_cast<int32_t>(std::floor(rig.camera.x)) && rig.camera.world_y == static_cast<int32_t>(std::floor(rig.camera.y)), at + "the whole parts of the origin follow it");
                    // the square is in view when it fits in the world that is seen (from a half-pixel origin the left / top edge of the square, a margin of 128 pixels around the ant, may end
                    // half a world pixel = one screen pixel outside: the scroll counts from the whole part of the origin)
                    const bool exact_level = z == 0.5f || z == 1.0f || z == 2.0f;
                    const float slack = exact_level ? (org.half_pixel ? 0.5f : 0.0f) : 1.0f + static_cast<float>(0.5 * zoom::grid(z)) + 1e-3f;          // (an origin that is not on the whole pixel: the scroll counts from its whole part, a world pixel, and the lattice rounds it by half a screen pixel)
                    if (r - l <= vis_w && b - t <= vis_h) {
                        check(rig.camera.x <= static_cast<float>(l) + slack && rig.camera.x + static_cast<float>(vis_w) + slack >= static_cast<float>(r) && rig.camera.y <= static_cast<float>(t) + slack && rig.camera.y + static_cast<float>(vis_h) + slack >= static_cast<float>(b),
                              at + "the ant's square is in the world that the view shows");
                    }
                }
            }
        }
    }
    check(moved_right > 8 && moved_down > 8, "the cases include views that have to move right and down to show the square: " + std::to_string(moved_right) + " and " + std::to_string(moved_down));
}
