// The wide match screen, part 3: the view (camera, edge strips, small maps, the minimap) and the application (the picture of every screen, the options, the pointer).
// (See test_wide_hud.cpp for what the whole program checks.)
#include "wide_hud_fixtures.hpp"

// =====================================================================================================================================================
// 7. The view: the camera, the edge strips, the start view, the minimap, small maps
// =====================================================================================================================================================

void test_camera_clamps() {
    group("camera", "the camera of the 762 x 500 view: limits on a big map, a map smaller than the view is centred and the camera is fixed there");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    auto camera = [&](bool centre) {
        ViewportCamera c;
        c.set_view(wide.view());
        c.centre_small_maps = centre;
        return c;
    };
    // a big map: 60 x 60 tiles = 1920 px; the view's origin lies in [0, 1920 - 762] x [0, 1920 - 500]
    {
        ViewportCamera c = camera(true);
        check(c.viewport_w == 762 && c.viewport_h == 500 && c.view_x == 16 && c.view_y == 21, "the camera takes the layout's view: 762 x 500 at (16, 21)");
        c.x = -300;
        c.y = -9;
        c.clamp_to_bounds(60, 60);
        check(c.world_x == 0 && c.world_y == 0, "a big map: a camera beyond the top left corner is at (0, 0)");
        c.x = 9000;
        c.y = 9000;
        c.clamp_to_bounds(60, 60);
        check(c.world_x == 1920 - 762 && c.world_y == 1920 - 500, "a big map: beyond the bottom right corner it is at (1158, 1420)");
        c.center_on(960, 960, 60, 60);
        check(c.world_x == 960 - 381 && c.world_y == 960 - 250, "center_on puts the point in the middle of the 762 x 500 view");
        c.center_on(10, 10, 60, 60);
        check(c.world_x == 0 && c.world_y == 0, "center_on near the corner is held at the map's corner");
        ViewportCamera classic;
        classic.x = 9000;
        classic.y = 9000;
        classic.clamp_to_bounds(60, 60);
        check(classic.world_x == 1920 - 442 && classic.world_y == 1920 - 440, "the original's view: the limits are (1478, 1480)");
    }
    // a map that is smaller than the view: 16 x 16 tiles = 512 px (762 wide: centred; 500 high: the map is 12 px taller than the view, so a range of 12)
    {
        ViewportCamera c = camera(true);
        c.x = 300;
        c.y = 300;
        c.clamp_to_bounds(16, 16);
        check(c.world_x == (512 - 762) / 2 && c.world_x == -125, "16 x 16: the camera is fixed at x = -125 (the map centred in the 762 px view)");
        check(c.world_y == 12, "16 x 16: vertically the map is 12 px taller than the view: the camera is held at 12 (the largest)");
        c.y = -50;
        c.clamp_to_bounds(16, 16);
        check(c.world_y == 0 && c.world_x == -125, "16 x 16: and at 0 (the smallest); x stays");
        c.center_on(256, 256, 16, 16);
        check(c.world_x == -125 && c.world_y == 6, "16 x 16: center_on centres the map in x and puts the point in the middle in y");
        // what is where: the map's left edge is 125 px right of the view's left edge
        int32_t sx = 0, sy = 0;
        c.world_to_screen(0, 0, sx, sy);
        check(sx == 16 + 125 && sy == 21 - 6, "the world pixel (0, 0) is at the screen pixel (141, 15)");
        c.world_to_screen(512, 512, sx, sy);
        check(sx == 16 + 125 + 512, "the map's right edge is at x = 653");
        // the listener is the middle of the view: the middle of the map
        check(c.world_x + 762 / 2 == 512 / 2, "the middle of the view is the middle of the map (the sound listener)");
        ViewportCamera d = camera(true);
        d.x = 5;
        d.y = 5;
        d.clamp_to_bounds(12, 12);
        check(d.world_x == (384 - 762) / 2 && d.world_y == (384 - 500) / 2 && d.world_x == -189 && d.world_y == -58, "12 x 12: both axes are centred: (-189, -58)");
        d.center_on(100, 100, 12, 12);
        check(d.world_x == -189 && d.world_y == -58, "12 x 12: center_on cannot move it");
        d.scroll_pixels(40, 40, 12, 12);
        check(d.world_x == -189 && d.world_y == -58, "12 x 12: scrolling cannot move it");
    }
    // a map exactly as wide as the view
    {
        ViewportCamera c = camera(true);
        c.x = 50;
        c.clamp_to_bounds(60, 60);
        ViewportCamera e;
        e.set_view(LayoutRect{16, 21, 640, 500});
        e.centre_small_maps = true;
        e.x = 77;
        e.clamp_to_bounds(20, 20);                       // 640 px = the view's width: the range is [0, 0]
        check(e.world_x == 0, "a map exactly as wide as the view: the camera is at 0");
    }
    // a map one pixel wider than the view has a range of one pixel (a view of 639 px, a map of 640)
    {
        ViewportCamera c;
        c.set_view(LayoutRect{16, 21, 639, 500});
        c.centre_small_maps = true;
        c.x = 5;
        c.clamp_to_bounds(20, 20);
        check(c.world_x == 1, "a map that is 1 px wider than the view: the camera may be at 1 (the range is [0, 1], not centred)");
        c.x = 0;
        c.clamp_to_bounds(20, 20);
        check(c.world_x == 0, "... and at 0");
    }
    // the original's camera keeps its rule: a map smaller than the view sits at the corner (the fingerprints of the classic picture pin it)
    {
        ViewportCamera c = camera(false);
        c.x = 40;
        c.y = 40;
        c.clamp_to_bounds(16, 16);
        check(c.world_x == 0 && c.world_y == 12, "without centre_small_maps: x = 0 (the old rule), y clamps to its range");
        ViewportCamera classic;
        classic.x = 40;
        classic.clamp_to_bounds(13, 9);
        check(classic.world_x == 0 && classic.world_y == 0, "the original's camera: a map smaller than the 442 x 440 view is at (0, 0)");
    }
    // the renderer sets the flag for every layout that is not the original's
    {
        const QuietStdout quiet;
        ensure_sdl();
        SDL_Window* win = SDL_CreateWindow("wide-hud", 0, 0, 640, 480, SDL_WINDOW_HIDDEN);
        assets::AssetArchive arc;
        arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
        Renderer r;
        if (win != nullptr && r.init(win, arc)) {
            check(!r.camera().centre_small_maps, "a new renderer has the original's camera rule");
            r.set_layout(wide);
            check(r.camera().centre_small_maps && r.camera().viewport_w == 762, "a wide layout centres small maps");
            r.set_layout(ScreenLayout::classic());
            check(!r.camera().centre_small_maps && r.camera().viewport_w == 442, "the original's layout does not");
            r.shutdown();
        } else {
            check(false, "a renderer for the camera rule");
        }
        if (win != nullptr) SDL_DestroyWindow(win);
    }
}

void test_edge_strips() {
    group("strips", "the edge strips run along the edges of the whole 960 x 540 picture: every pixel, the scroll arrows and the inner strips; a map smaller than the view has none");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    // an independent rule: the 12 px band along the edges gives the arrow of its side, the corners the diagonal ones
    auto expected_dir = [](int32_t x, int32_t y) {
        const int32_t h = x < 12 ? -1 : (x >= 948 ? 1 : 0);
        const int32_t v = y < 12 ? -1 : (y >= 528 ? 1 : 0);
        // N NE E SE S SW W NW
        if (v == -1 && h == 0) return 0;
        if (v == -1 && h == 1) return 1;
        if (v == 0 && h == 1) return 2;
        if (v == 1 && h == 1) return 3;
        if (v == 1 && h == 0) return 4;
        if (v == 1 && h == -1) return 5;
        if (v == 0 && h == -1) return 6;
        if (v == -1 && h == -1) return 7;
        return -1;
    };
    // a camera in the middle of a big map: every strip can scroll
    int64_t wrong = 0, bands = 0;
    for (int32_t y = 0; y < 540; ++y) {
        for (int32_t x = 0; x < 960; ++x) {
            const EdgeScroll e = edge_scroll_step(x, y, 50, 600, 600, 60, 60, wide);
            if (e.dir != expected_dir(x, y)) ++wrong;
            if (e.dir >= 0) ++bands;
        }
    }
    check(wrong == 0, "from the middle of a 60 x 60 map every one of the 518,400 pixels gives the arrow of the band it is in (" + std::to_string(wrong) + " wrong)");
    {
        // the bands are exactly the picture's frame: the pixels with x < 12 or x >= 948 or y < 12 or y >= 528 (and the quiet margin of 13 does not change that)
        int64_t frame = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) frame += (x < 12 || x >= 948 || y < 12 || y >= 528) ? 1 : 0;
        }
        check(bands == frame, "the pixels with an arrow are the 12 px frame of the 960 x 540 picture (" + std::to_string(frame) + ")");
    }
    // the inner 5 px strips move the view, the rest of the bands only show the arrow
    {
        const EdgeScroll east_inner = edge_scroll_step(958, 270, 50, 600, 600, 60, 60, wide);
        const EdgeScroll east_band = edge_scroll_step(950, 270, 50, 600, 600, 60, 60, wide);
        check(east_inner.dir == 2 && east_inner.dx > 0 && east_band.dir == 2 && east_band.dx == 0 && east_band.dy == 0, "the east strip: the inner 5 px scroll east, the rest of the 12 px band does not");
        const EdgeScroll south_inner = edge_scroll_step(480, 537, 50, 600, 600, 60, 60, wide);
        check(south_inner.dir == 4 && south_inner.dy > 0, "the south strip scrolls south");
        const EdgeScroll classic_east = edge_scroll_step(700, 270, 50, 600, 600, 60, 60);
        check(classic_east.dir == -1, "(at x = 700 the original's picture has no strip: the strips are the layout's)");
    }
    // at the map's corners only the strips that can move the view show an arrow
    {
        const EdgeScroll n_at_top = edge_scroll_step(480, 3, 50, 100, 0, 60, 60, wide);
        const EdgeScroll s_at_top = edge_scroll_step(480, 537, 50, 100, 0, 60, 60, wide);
        const EdgeScroll w_at_left = edge_scroll_step(3, 270, 50, 0, 100, 60, 60, wide);
        const EdgeScroll e_at_right = edge_scroll_step(958, 270, 50, 1920 - 762, 100, 60, 60, wide);
        check(n_at_top.dir == -1 && s_at_top.dir == 4 && w_at_left.dir == -1 && e_at_right.dir == -1, "a strip that cannot move the view shows no arrow (north at the top, west at the left, east at the right end of the map)");
    }
    // a map that is smaller than the view in both directions: no strip scrolls anywhere (12 x 12 tiles, the camera centred at (-189, -58))
    {
        int64_t arrows = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) arrows += edge_scroll_step(x, y, 50, -189, -58, 12, 12, wide).dir >= 0 ? 1 : 0;
        }
        check(arrows == 0, "a 12 x 12 map is smaller than the view in both directions: no pixel scrolls (" + std::to_string(arrows) + ")");
    }
    // 16 x 16: 512 px: narrower than the view (no east, west) and 12 px taller (north / south and the corners that have a vertical part)
    {
        const EdgeScroll e = edge_scroll_step(958, 270, 50, -125, 6, 16, 16, wide);
        const EdgeScroll w = edge_scroll_step(3, 270, 50, -125, 6, 16, 16, wide);
        const EdgeScroll n = edge_scroll_step(480, 3, 50, -125, 6, 16, 16, wide);
        const EdgeScroll s = edge_scroll_step(480, 537, 50, -125, 6, 16, 16, wide);
        check(e.dir == -1 && w.dir == -1 && n.dir == 0 && s.dir == 4, "16 x 16: only the north and south strips can move the view (it is 12 px taller than the 500 px view)");
        check(n.dy != 0 || n.dir == 0, "16 x 16: ... and they scroll");
    }
}

void test_start_view_and_minimap(const assets::AssetArchive& arc) {
    group("view", "the start view and the minimap follow the 762 x 500 view");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    // the start view: the square around the hill (ax - 160 .. ax + 192) clipped to the map, scrolled just far enough to show it in a 762 x 500 view: an independent rule
    int64_t wrong = 0, count = 0;
    for (const int32_t tiles : {60, 40, 31, 22, 14, 13}) {
        for (int32_t ty = 0; ty < tiles; ++ty) {
            for (int32_t tx = 0; tx < tiles; ++tx) {
                const int32_t ax = tx * 32 + 16, ay = ty * 32 + 16, map_px = tiles * 32;
                const int32_t r = std::min(ax + 192, map_px), b = std::min(ay + 192, map_px);
                const int32_t want_x = std::max(0, r - 762), want_y = std::max(0, b - 500);
                int32_t ox = -1, oy = -1;
                start_view_origin(tx, ty, tiles, tiles, ox, oy, wide);
                wrong += (ox != want_x || oy != want_y) ? 1 : 0;
                ++count;
            }
        }
    }
    check(wrong == 0, "the start view of every anchor tile of six maps is the one that shows the square in a 762 x 500 view (" + std::to_string(count) + " tiles, " + std::to_string(wrong) + " wrong)");
    {
        int32_t cx = 0, cy = 0, wx = 0, wy = 0;
        start_view_origin(46, 46, 60, 60, cx, cy);
        start_view_origin(46, 46, 60, 60, wx, wy, wide);
        check(cx == 1238 && cy == 1240 && wx == 918 && wy == 1180, "the far hill (46, 46) of a 60 x 60 map: the original's start view is (1238, 1240), the 762 x 500 view's (918, 1180)");
    }
    // the minimap: its frame is the view's size scaled to the image (+ 1), the whole image for a map the view covers
    auto frame_of = [&](uint32_t tiles, int32_t cam_x, int32_t cam_y) {
        sim::WorldState world;
        world.width = tiles;
        world.height = tiles;
        world.cells.assign(static_cast<size_t>(tiles) * tiles, sim::TileCell{});
        HUD hud;
        hud.set_layout(wide);
        ViewportCamera cam;
        cam.set_view(wide.view());
        cam.centre_small_maps = true;
        cam.x = static_cast<float>(cam_x);
        cam.y = static_cast<float>(cam_y);
        cam.clamp_to_bounds(tiles, tiles);
        Spy spy(arc);
        hud.render(spy, arc, world, cam);
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Rect && e.color.r == 251 && e.color.g == 251 && e.color.b == 255) return LayoutRect{e.x, e.y, e.w, e.h};
        }
        return LayoutRect{-1, -1, -1, -1};
    };
    check_rect(frame_of(60, 0, 0), LayoutRect{800, 35, 48, 24}, "60 x 60: the view frame is 48 x 24 at the corner of the minimap (762 * 119 / 1920 + 1, 500 * 91 / 1920 + 1)");
    check_rect(frame_of(60, 600, 700), LayoutRect{800 + 600 * 119 / 1920, 35 + 700 * 91 / 1920, 48, 24}, "60 x 60: and it follows the camera");
    check_rect(frame_of(40, 0, 0), LayoutRect{800, 35, 71, 36}, "40 x 40: a bigger share of the map: 71 x 36");
    check_rect(frame_of(16, 0, 0), LayoutRect{800, 35, 119, 89}, "16 x 16: the view covers the 512 px wide map (the whole image's width) and 500 of its 512 px height (500 * 91 / 512 + 1 = 89)");
    check_rect(frame_of(12, 0, 0), LayoutRect{800, 35, 119, 91}, "12 x 12: the whole map is in the view: the frame is the whole image");
}

void test_small_map_pointer(const assets::AssetArchive& arc) {
    group("small-map", "on a map smaller than the view the black around it is no ground: the plain pointer, a click there does nothing at all (no deselect, no order, no marker, no pedestal change); the map itself works");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    HudRig rig(arc, wide, 0, 16);                                    // 16 x 16 tiles: the camera is at (-125, 12 at most)
    const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{4, 4});
    rig.sim.tick();
    rig.cam.x = 0;
    rig.cam.y = 0;
    rig.cam.clamp_to_bounds(16, 16);
    check(rig.cam.world_x == -125 && rig.cam.world_y == 0, "the camera of the 16 x 16 map is at (-125, 0)");
    std::vector<std::pair<int32_t, int32_t>> markers;
    rig.hud.set_on_spawn_click_marker([&markers](int32_t x, int32_t y) { markers.emplace_back(x, y); });
    const sim::WorldState world = rig.sim.get_world_state();
    rig.hud.select_ant(ant);
    rig.hud.update(world, 0);
    // the map starts at screen x = 16 + 125 = 141: left of it is black, and so is x >= 653
    const int32_t y = 300;
    check(rig.hud.evaluate_cursor(100, y, world, rig.sim.grid(), rig.cam) == CursorType::Normal, "over the black left of the map: the plain pointer");
    check(rig.hud.evaluate_cursor(140, y, world, rig.sim.grid(), rig.cam) == CursorType::Normal, "... at its last pixel (x = 140)");
    check(rig.hud.evaluate_cursor(141, y, world, rig.sim.grid(), rig.cam) == CursorType::Move, "the map's first column (x = 141) is ground: the move cursor");
    check(rig.hud.evaluate_cursor(652, y, world, rig.sim.grid(), rig.cam) == CursorType::Move, "its last column (x = 652)");
    check(rig.hud.evaluate_cursor(653, y, world, rig.sim.grid(), rig.cam) == CursorType::Normal, "... and the black right of it (x = 653)");
    // the map's last row: world y 511 is at screen y 21 + 511 = 532 - ... the view ends at 521: the whole view is ground here (the map is taller than the view's 500 only by 12)
    check(rig.hud.evaluate_cursor(300, 21, world, rig.sim.grid(), rig.cam) == CursorType::Move && rig.hud.evaluate_cursor(300, 520, world, rig.sim.grid(), rig.cam) == CursorType::Move, "the top and the bottom row of the view are ground (the map is 12 px taller than the view)");
    // a click on the black does nothing at all: the selection stays, the latched pedestal stays, no marker, no order; a click on the map orders
    {
        const int32_t pedestal_x = wide.right(482) + 12, pedestal_y = 152 + 12;                 // the Move pedestal's slot (482, 152) 43 x 73 + (dx, 0)
        rig.click(pedestal_x, pedestal_y);
        check(rig.hud.is_move_latched(), "(the Move pedestal is latched by a click on it)");
        rig.click(100, y);
        check(markers.empty() && rig.hud.get_selected_ant_id() == ant && rig.hud.get_selected_ant_ids().size() == 1 && rig.hud.is_move_latched(), "a click on the black left of the map: the ant stays selected, the pedestal stays latched, no marker");
        rig.click(700, y);
        check(markers.empty() && rig.hud.get_selected_ant_id() == ant && rig.hud.is_move_latched(), "... and on the black right of it");
        rig.click(100, y, SDL_BUTTON_RIGHT);
        check(markers.empty() && rig.hud.get_selected_ant_id() == ant && rig.hud.is_move_latched(), "a right click on the black orders nothing (no marker either)");
        // pressed on the black, released on the map: the order's tile would be the press point, which is no ground
        rig.hud.handle_mouse_down(100, y, SDL_BUTTON_RIGHT, rig.sim, rig.cam);
        rig.hud.handle_mouse_up(300, y, SDL_BUTTON_RIGHT, rig.sim, rig.cam);
        check(markers.empty() && rig.hud.is_move_latched(), "a right press on the black released on the map orders nothing either");
        rig.hud.unlatch_pedestals();
    }
    rig.click(300, y);
    check(markers.size() == 1 && markers[0].first == 300 - 16 - 125 && markers[0].second == y - 21, "a click on the map orders the world point under the pointer (159, 279)");
    // a rubber band over the black selects the ants of the map under it and does not fail
    rig.hud.clear_selection();
    rig.hud.handle_mouse_down(20, 40, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    rig.hud.handle_mouse_motion(700, 500, rig.sim, rig.cam);
    rig.hud.handle_mouse_up(700, 500, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    check(rig.hud.get_selected_ant_id() == ant, "a rubber band from the black left of the map to the black right of it selects the own ant on the map");
}

/// A map that is smaller than the view in y as well as in x: the black is above and below the map, and the boundary rows decide the pointer
void test_small_map_pointer_rows(const assets::AssetArchive& arc) {
    group("small-map-rows", "a map smaller than the view on both axes (12 x 12): the pointer's boundary on all four sides, the black above and below is no ground");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    HudRig rig(arc, wide, 0, 12);                                     // 384 x 384 px: the camera is at (-189, -58)
    const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{4, 4});
    rig.sim.tick();
    rig.cam.x = 0;
    rig.cam.y = 0;
    rig.cam.clamp_to_bounds(12, 12);
    check(rig.cam.world_x == -189 && rig.cam.world_y == -58, "the camera of the 12 x 12 map is at (-189, -58)");
    std::vector<std::pair<int32_t, int32_t>> markers;
    rig.hud.set_on_spawn_click_marker([&markers](int32_t x, int32_t y) { markers.emplace_back(x, y); });
    const sim::WorldState world = rig.sim.get_world_state();
    rig.hud.select_ant(ant);
    rig.hud.update(world, 0);
    // the map covers the screen x 205 .. 588 and y 79 .. 462
    const int32_t mid_x = 400, mid_y = 270;
    auto cursor = [&](int32_t x, int32_t y) { return rig.hud.evaluate_cursor(x, y, world, rig.sim.grid(), rig.cam); };
    check(cursor(mid_x, 78) == CursorType::Normal && cursor(mid_x, 40) == CursorType::Normal, "above the map (y = 78 and 40): the plain pointer");
    check(cursor(mid_x, 79) == CursorType::Move && cursor(mid_x, 462) == CursorType::Move, "the map's first and last row (y = 79 and 462): ground");
    check(cursor(mid_x, 463) == CursorType::Normal && cursor(mid_x, 500) == CursorType::Normal, "below the map (y = 463 and 500): the plain pointer");
    check(cursor(204, mid_y) == CursorType::Normal && cursor(205, mid_y) == CursorType::Move && cursor(588, mid_y) == CursorType::Move && cursor(589, mid_y) == CursorType::Normal, "left and right of the map: the boundary is x = 204 | 205 and 588 | 589");
    // the corners of the map
    check(cursor(205, 79) == CursorType::Move && cursor(588, 462) == CursorType::Move && cursor(204, 78) == CursorType::Normal && cursor(589, 463) == CursorType::Normal, "the corners: (205, 79) and (588, 462) are ground, one pixel outside is not");
    check(rig.hud.over_ground(mid_x, 79, rig.cam, rig.sim.grid()) && !rig.hud.over_ground(mid_x, 78, rig.cam, rig.sim.grid()) && !rig.hud.over_ground(mid_x, 463, rig.cam, rig.sim.grid()) && !rig.hud.over_ground(204, mid_y, rig.cam, rig.sim.grid()), "over_ground has the same boundary");
    // a click one pixel above the map does nothing at all, one pixel inside it orders
    rig.click(mid_x, 78);
    check(markers.empty() && rig.hud.get_selected_ant_id() == ant, "a click at y = 78 (just above the map): no marker, the ant stays selected");
    rig.click(mid_x, 463);
    check(markers.empty() && rig.hud.get_selected_ant_id() == ant, "a click at y = 463 (just below it): nothing");
    rig.click(mid_x, 79);
    check(markers.size() == 1 && markers[0].second == 79 - 21 - 58, "a click at y = 79 (the map's first row): it orders the world point (211, 0)");
}

namespace {

// --- a small level made from the bytes of TINY.LVL (nothing of the community is in the repository): the columns `cols` and the rows `rows` of its two layers and the records of blocks 1 and 2
// that lie inside them, shifted; the file format is the loader's (header, dictionary, the two dimension dwords, two layers of 3 words a cell, block 1, block 2, block 3, block 4, the egg stock)
struct LevelBytes {
    std::vector<uint8_t> d;
    size_t p{0};
    uint16_t u16() { const uint16_t v = static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); p += 2; return v; }
    uint32_t u32() { const uint32_t lo = u16(); const uint32_t hi = u16(); return lo | (hi << 16); }
};

void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(static_cast<uint8_t>(v & 0xFF)); o.push_back(static_cast<uint8_t>(v >> 8)); }
void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, static_cast<uint16_t>(v & 0xFFFF)); put16(o, static_cast<uint16_t>(v >> 16)); }

/// Writes a level of cols.size() x rows.size() tiles cut out of TINY.LVL to `path`; false if TINY.LVL cannot be read
bool write_small_level(const std::string& path, const std::vector<int32_t>& cols, const std::vector<int32_t>& rows) {
    LevelBytes in;
    {
        std::ifstream f(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL", std::ios::binary);
        if (!f) return false;
        in.d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    std::vector<uint8_t> out;
    const uint32_t version = in.u32(), mode = in.u32();
    const uint16_t minutes = in.u16();
    const size_t desc_at = in.p;
    in.p += 30;
    const uint16_t tcount = in.u16();
    const size_t dict_at = in.p;
    in.p += (static_cast<size_t>(tcount) + 1) * 11;
    const uint32_t src_rows = in.u32(), src_cols = in.u32();
    const size_t n = static_cast<size_t>(src_rows) * src_cols;
    std::vector<std::array<uint16_t, 3>> l1(n), l2(n);
    for (auto& c : l1) c = {in.u16(), in.u16(), in.u16()};
    for (auto& c : l2) c = {in.u16(), in.u16(), in.u16()};
    put32(out, version);
    put32(out, mode);
    put16(out, minutes);
    out.insert(out.end(), in.d.begin() + static_cast<std::ptrdiff_t>(desc_at), in.d.begin() + static_cast<std::ptrdiff_t>(desc_at) + 30);
    put16(out, tcount);
    out.insert(out.end(), in.d.begin() + static_cast<std::ptrdiff_t>(dict_at), in.d.begin() + static_cast<std::ptrdiff_t>(dict_at) + static_cast<std::ptrdiff_t>((static_cast<size_t>(tcount) + 1) * 11));
    put32(out, static_cast<uint32_t>(rows.size()));
    put32(out, static_cast<uint32_t>(cols.size()));
    for (const auto* layer : {&l1, &l2}) {
        for (const int32_t r : rows) {
            for (const int32_t c : cols) {
                const auto& cell = (*layer)[static_cast<size_t>(r) * src_cols + static_cast<size_t>(c)];
                put16(out, cell[0]);
                put16(out, cell[1]);
                put16(out, cell[2]);
            }
        }
    }
    auto index_of = [](const std::vector<int32_t>& v, int32_t x) -> int32_t {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == x) return static_cast<int32_t>(i);
        }
        return -1;
    };
    // block 1: (tile, row, column)
    const uint16_t b1_count = in.u16();
    std::vector<std::array<uint16_t, 3>> b1;
    for (uint16_t i = 0; i < b1_count; ++i) {
        const uint16_t tile = in.u16(), row = in.u16(), col = in.u16();
        const int32_t nr = index_of(rows, row), nc = index_of(cols, col);
        if (nr >= 0 && nc >= 0) b1.push_back({tile, static_cast<uint16_t>(nr), static_cast<uint16_t>(nc)});
    }
    put16(out, static_cast<uint16_t>(b1.size()));
    for (const auto& r : b1) for (const uint16_t v : r) put16(out, v);
    // block 2: food (row, column, units, points, stage count, stages)
    const uint16_t food_count = in.u16();
    std::vector<std::vector<uint16_t>> food;
    for (uint16_t i = 0; i < food_count; ++i) {
        std::vector<uint16_t> rec = {in.u16(), in.u16(), in.u16(), in.u16(), in.u16()};
        for (uint16_t k = 0; k < rec[4]; ++k) {
            rec.push_back(in.u16());
            rec.push_back(in.u16());
        }
        const int32_t nr = index_of(rows, rec[0]), nc = index_of(cols, rec[1]);
        if (nr >= 0 && nc >= 0) {
            rec[0] = static_cast<uint16_t>(nr);
            rec[1] = static_cast<uint16_t>(nc);
            food.push_back(rec);
        }
    }
    put16(out, static_cast<uint16_t>(food.size()));
    for (const auto& rec : food) for (const uint16_t v : rec) put16(out, v);
    const uint16_t b3a = in.u16(), b3b = in.u16();                                   // block 3 (the default ant type): as TINY has it
    put16(out, b3a);
    put16(out, b3b);
    put16(out, 0);                                                                    // block 4: no waypoints
    put16(out, 3);                                                                    // the egg stock
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

}  // namespace

/// What hangs over the edge of a map that is smaller than the view is cut at the edge: the black around the map stays black (an ant, an effect or a tall sprite at the edge used to draw
/// onto it: the clip was the view, not the view that the map covers)
void test_small_map_clip(const assets::AssetArchive& arc) {
    group("small-map-draw", "on a map smaller than the view nothing draws onto the black around it: the clip is the part of the view that the map covers");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    // the clip rectangle: view and map intersected
    {
        RendererRig rig(arc, 960, 540);
        check(rig.ok, "the renderer rig is up");
        if (!rig.ok) return;
        rig.renderer.set_layout(wide);
        const LayoutRect view = wide.view();
        struct Case { uint32_t tiles; LayoutRect want; const char* what; };
        const Case cases[] = {{15, LayoutRect{157, 31, 480, 480}, "15 x 15 (480 px: smaller than the view on both axes, camera (-141, -10))"},
                              {12, LayoutRect{205, 79, 384, 384}, "12 x 12 (384 px: camera (-189, -58))"},
                              {16, LayoutRect{141, 21, 512, 500}, "16 x 16 (512 px wide, 12 px taller than the view: only x is cut)"},
                              {31, view, "31 x 31 (992 px: bigger than the view on both axes: the whole view)"},
                              {60, view, "60 x 60: the whole view"}};
        for (const Case& c : cases) {
            rig.renderer.camera().x = 0.0f;
            rig.renderer.camera().y = 0.0f;
            rig.renderer.camera().clamp_to_bounds(c.tiles, c.tiles);
            check_rect(rig.renderer.map_view_rect(c.tiles, c.tiles), c.want, c.what);
        }
        // a map as wide as the view or wider whose camera is scrolled to its far edge: the view still
        rig.renderer.camera().x = 100000.0f;
        rig.renderer.camera().y = 100000.0f;
        rig.renderer.camera().clamp_to_bounds(60, 60);
        check_rect(rig.renderer.map_view_rect(60, 60), view, "60 x 60 with the camera at its far corner: the whole view");
        // the original's own picture: every map is bigger than its view, the clip is the view
        RendererRig classic(arc, 640, 480);
        if (classic.ok) {
            classic.renderer.set_layout(ScreenLayout::classic());
            classic.renderer.camera().clamp_to_bounds(31, 31);
            check_rect(classic.renderer.map_view_rect(31, 31), ScreenLayout::classic().view(), "classic: a 31 x 31 map: the view (16, 21, 442 x 440)");
        }
    }
    // the pixels: ants half out of a 15 x 15 map at its four edges and corners leave the black black, and are drawn on the map
    {
        RendererRig rig(arc, 960, 540);
        if (!rig.ok) return;
        rig.renderer.set_layout(wide);
        rig.renderer.set_hud_team(0);
        sim::SimulationEngine sim;
        sim.init_test_world(15, 15, 1, 600000);
        sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{7, 7});
        sim.tick();
        sim::WorldState world = sim.get_world_state();
        check(!world.ants.empty(), "an ant is in the world");
        if (world.ants.empty()) return;
        const sim::AntSnapshot proto = world.ants.front();
        world.ants.clear();
        const std::pair<int32_t, int32_t> spots[] = {{0, 240}, {479, 240}, {240, 0}, {240, 479}, {0, 0}, {479, 0}, {0, 479}, {479, 479}};
        uint32_t next = 1000;
        for (const auto& spot : spots) {
            sim::AntSnapshot a = proto;
            a.id = next++;
            a.px = spot.first;
            a.py = spot.second;
            a.tile_x = spot.first / 32;
            a.tile_y = spot.second / 32;
            world.ants.push_back(a);
        }
        rig.renderer.camera().x = 0.0f;
        rig.renderer.camera().y = 0.0f;
        rig.renderer.camera().clamp_to_bounds(15, 15);
        rig.renderer.begin_frame();
        rig.renderer.render_world(world, sim.grid(), 0, {}, false, false, 0, 0, -1, 0.0f);
        const std::vector<uint8_t> px = rig.read();
        const LayoutRect view = wide.view();
        const LayoutRect map{157, 31, 480, 480};
        int64_t stray = 0, ground_changed = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) {
                const size_t i = (static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4;
                const bool black = px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0;
                if (!map.contains(x, y) && !black) ++stray;                          // nothing but black outside the map (the frame was cleared black and no HUD is drawn)
                if (map.contains(x, y) && !(px[i] == 135 && px[i + 1] == 120 && px[i + 2] == 110)) ++ground_changed;
            }
        }
        (void)view;
        check(stray == 0, "nothing is drawn outside the map: " + std::to_string(stray) + " pixels of the black around it are not black");
        check(ground_changed > 200, "the ants are drawn on the map (" + std::to_string(ground_changed) + " pixels are not plain ground)");
    }
}

namespace {

// =====================================================================================================================================================
// 8. The application: the picture of each screen, the pointer, the margin, the default
// =====================================================================================================================================================

SDL_Window* find_window() {
    for (uint32_t i = 1; i < 256; ++i) {
        if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
    }
    return nullptr;
}

std::filesystem::path temp_ini(const char* stem) {
    static int counter = 0;
    return std::filesystem::temp_directory_path() / (std::string(stem) + "_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(counter++) + ".ini");
}

/// An application on the dummy video driver, headless (a hidden window), with its settings in a file of this run
struct AppFixture {
    AppFixture(Aspect aspect, bool match, int32_t window_w = 0, int32_t window_h = 0, const std::string& settings_text = std::string(), bool aspect_given = true, bool play = true) : settings(temp_ini("ants_wide_hud")) {
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        if (!settings_text.empty()) {
            std::ofstream out(settings);
            out << settings_text;
        }
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.aspect = aspect;
        cfg.aspect_given = aspect_given;
        cfg.start_in_map_select = !match;
        cfg.settings_path = settings.string();
        if (window_w > 0) {
            cfg.has_window_size = true;
            cfg.window_w = window_w;
            cfg.window_h = window_h;
        }
        QuietStdout quiet;
        ok = app.init(cfg);
        if (ok && match && play) ok = app.start_game("Original-Ants/Maps/SMALL.LVL");
        if (ok && match && play) app.hud().update(app.sim().get_world_state(), 100);        // (the start dialog is gone)
        window = find_window();
    }
    ~AppFixture() {
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    }
    AppFixture(const AppFixture&) = delete;
    AppFixture& operator=(const AppFixture&) = delete;

    void motion_at_canvas(float cx, float cy) {
        int wx = 0, wy = 0;
        SDL_RenderLogicalToWindow(app.renderer().get_sdl_renderer(), cx, cy, &wx, &wy);
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = wx;
        e.motion.y = wy;
        SDL_PushEvent(&e);
    }
    void deliver() { app.run_frame_with_delta(0.001f); }
    std::vector<uint8_t> canvas_pixels() {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(app.renderer().get_sdl_renderer(), &w, &h);
        std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
        SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
        return px;
    }

    std::filesystem::path settings;
    Application app;
    SDL_Window* window{nullptr};
    bool ok{false};
};

std::array<uint8_t, 3> pixel(const std::vector<uint8_t>& px, int32_t width, int32_t x, int32_t y) {
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    return {px[i], px[i + 1], px[i + 2]};
}

}  // namespace

void test_picture_per_screen() {
    group("screens", "a match and every screen outside it (the setup screen, the loading screen, the quick help, the results, the start menu) are the whole 960 x 540 canvas; the pointer stays where it is");
    const LayoutRect whole{0, 0, 960, 540};
    const LayoutRect page{160, 30, 640, 480};      // (a match of the original's own layout in this canvas is centred: the only picture that is not the whole canvas)
    {
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        check(f.ok, "the 16:9 application starts on its setup screen");
        if (!f.ok) return;
        check(f.app.aspect() == Aspect::Wide16x9 && f.app.canvas() == CanvasLayout{960, 540}, "the canvas is 960 x 540");
        check(f.app.layout() == ScreenLayout::with_size(960, 540) && f.app.hud().layout() == f.app.layout() && f.app.renderer().layout() == f.app.layout(), "the application, the HUD and the renderer hold the wide layout (the match screen's) already on the setup screen");
        check_rect(f.app.picture(), whole, "the setup screen has its own wide version: the whole canvas (so has every other screen: test_wide_pages; the wide setup screen is pinned in test_wide_setup)");
        check_rect(f.app.renderer().picture(), whole, "... and the renderer draws it there");
        // the pointer: the match starts with it in the middle of its picture (the original puts it there until it is seen moving)
        f.app.note_pointer(100, 90);
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match starts");
        check_rect(f.app.picture(), whole, "the match is the whole canvas");
        check_rect(f.app.renderer().picture(), whole, "... in the renderer too");
        check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270 && !f.app.mouse_has_moved(), "the pointer starts in the middle of the match's picture: (480, 270)");
        check(f.app.state() == AppState::Playing, "the application plays");
        // the results are the whole canvas too (the wide results page, results_layout.hpp): the picture does not change, so the pointer does not move (it used to move with the corner of a centred page)
        f.app.hud().update(f.app.sim().get_world_state(), 100);
        f.app.note_pointer(300, 200);
        sim::MatchResult result;
        result.is_over = true;
        result.ally = {255, 255, 255, 255};
        result.decide_winners();
        f.app.scorecard().show(result, 0);
        f.app.update_results(0.01f);
        check(f.app.scorecard().is_open(), "the results are open");
        check_rect(f.app.picture(), whole, "the results screen is the whole canvas (its wide page)");
        check(f.app.scorecard().wide_layout(), "... and the results screen is told that it is the wide page");
        check(f.app.mouse_screen_x() == 300 && f.app.mouse_screen_y() == 200, "the pointer stays: the match's (300, 200) is the results' (300, 200)");
        // back to the setup screen: the whole canvas, the pointer stays where it is on the canvas
        f.app.return_to_map_select();
        check_rect(f.app.picture(), whole, "back on the setup screen: the whole canvas");
        check(f.app.mouse_screen_x() == 300 && f.app.mouse_screen_y() == 200 && !f.app.scorecard().is_open(), "the pointer stays");
        // the way back from a match that is still running (not from its results): the setup screen is the whole canvas at once
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a second match starts");
        check_rect(f.app.picture(), whole, "the second match is the whole canvas");
        f.app.return_to_map_select();
        check_rect(f.app.picture(), whole, "back on the setup screen from a running match: the whole canvas, at once");
        // and the loading screen / quick help: the whole canvas too
        f.app.finish_loading();
        check(f.app.state() == AppState::QuickHelp || f.app.state() == AppState::MapSelect, "the loading screen ends in the quick help or the setup screen");
        check_rect(f.app.picture(), whole, "the quick help (or the setup screen) is the whole canvas");
    }
    {   // the original's own aspect: one picture, the whole canvas, in every screen
        AppFixture f(Aspect::Classic4x3, false, 640, 480);
        check(f.ok, "the 4:3 application starts");
        if (!f.ok) return;
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "4:3: the setup screen is the whole canvas");
        check(f.app.layout().is_classic(), "4:3: the layout is the original's");
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "4:3: a match starts");
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "4:3: the match is the whole canvas");
        sim::MatchResult result;
        result.is_over = true;
        result.ally = {255, 255, 255, 255};
        result.decide_winners();
        f.app.scorecard().show(result, 0);
        f.app.update_results(0.01f);
        check(f.app.scorecard().is_open(), "4:3: the results are open");
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "4:3: the results are the whole canvas");
    }
    {   // the pointer anywhere on the canvas when the results open: the results are the whole canvas, so every canvas point is a point of the picture and nothing is held at a page's edge (the
        // clamp to a centred page's nearest edge pixel, which this block used to pin, is only for a picture that is smaller than the canvas: the match of a classic layout, below)
        AppFixture f(Aspect::Wide16x9, true, 960, 540);
        check(f.ok, "the 16:9 application runs a match (the pointer where the results open)");
        if (f.ok) {
            struct Case { int32_t x, y; const char* what; };
            const Case cases[] = {{100, 270, "left (100, 270)"}, {900, 270, "right (900, 270)"}, {480, 10, "top (480, 10)"}, {480, 535, "bottom (480, 535)"}, {5, 5, "the corner (5, 5)"}, {955, 535, "the corner (955, 535)"}, {300, 200, "inside (300, 200)"}};
            for (const Case& c : cases) {
                AppFixture g(Aspect::Wide16x9, true, 960, 540);
                g.app.note_pointer(c.x, c.y);
                sim::MatchResult result;
                result.is_over = true;
                result.ally = {255, 255, 255, 255};
                result.decide_winners();
                g.app.scorecard().show(result, 0);
                g.app.update_results(0.01f);
                check(g.app.mouse_screen_x() == c.x && g.app.mouse_screen_y() == c.y && g.app.picture() == whole, std::string("the results screen, the pointer ") + c.what + " stays: (" + std::to_string(g.app.mouse_screen_x()) + ", " + std::to_string(g.app.mouse_screen_y()) + ")");
            }
            // the way back from a match to the setup screen: the whole canvas too, so the pointer stays exactly where it is
            f.app.note_pointer(100, 270);
            f.app.return_to_map_select();
            check(f.app.mouse_screen_x() == 100 && f.app.mouse_screen_y() == 270, "the setup screen (the whole canvas), from a running match: the pointer stays where it is");
            check(f.app.start_game("Original-Ants/Maps/SMALL.LVL") && f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270, "a match starts: the pointer is in the middle of the picture, as always");
            // a picture that is smaller than the canvas (a match of the original's layout, the test hook set_layout) still holds a pointer that was beside it at its nearest edge pixel
            f.app.note_pointer(100, 270);
            f.app.set_layout(ScreenLayout::classic());
            check_rect(f.app.picture(), page, "set_layout(classic): the 640 x 480 picture, centred");
            check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 240, "... a pointer left of it is held at its left edge, level with the pointer (the page's rule, for a picture smaller than the canvas)");
            f.app.set_layout(ScreenLayout::with_size(960, 540));
        }
    }
    {   // a match that starts at once (--map): the match screen from the first frame, the pointer in the middle of it
        AppFixture f(Aspect::Wide16x9, true, 960, 540, std::string(), true, false);
        check(f.ok && f.app.state() == AppState::Playing, "a 16:9 application that starts in its match");
        if (f.ok) {
            check_rect(f.app.picture(), whole, "16:9, started in a match (no start_game): the picture is the whole canvas");
            check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270, "... and the pointer is in the middle of it: (480, 270)");
            // a layout that is set is a picture of its own size at once (the match's picture is the layout's, centred in the canvas)
            f.app.set_layout(ScreenLayout::classic());
            check_rect(f.app.picture(), page, "set_layout(classic) in a match of a 16:9 canvas: the picture is the 640 x 480 one, centred, at once");
            f.app.set_layout(ScreenLayout::with_size(960, 540));
            check_rect(f.app.picture(), whole, "set_layout(960 x 540): the whole canvas again, at once");
        }
    }
    {   // the pointer of a game that opens on a screen starts in the middle of the picture
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        check(f.ok && f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270, "a 16:9 application on its setup screen: the pointer starts in the middle of the picture, (480, 270)");
    }
}

void test_small_levels_in_the_application() {
    group("small-levels", "whole application frames of matches on small levels cut from TINY.LVL: the black around the map stays black");
    // the review's 15 x 15 level (columns and rows 6 - 23 of TINY.LVL without 12 - 14: a gummy worm hung 65 px over its edge) and an 18 x 31 one that is only narrow: a whole application frame
    // of a match on each, the black around the map is black. (The HUD is drawn over the frame's edge, so only the part of the view that the map does not cover is looked at.)
    for (int variant = 0; variant < 2; ++variant) {
        std::vector<int32_t> cols, rows;
        for (int32_t i = 6; i <= 23; ++i) {
            if (variant == 0 && i >= 12 && i <= 14) continue;
            cols.push_back(i);
        }
        if (variant == 0) rows = cols;
        else for (int32_t i = 0; i <= 30; ++i) rows.push_back(i);
        const std::filesystem::path level = std::filesystem::temp_directory_path() / ("ants_wide_small_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(variant) + ".lvl");
        const std::string name = std::to_string(cols.size()) + " x " + std::to_string(rows.size()) + " level";
        check(write_small_level(level.string(), cols, rows), "the " + name + " is written from TINY.LVL");
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); continue; }
        check(f.app.start_game(level.string()), "a match on the " + name + " starts");
        f.app.hud().update(f.app.sim().get_world_state(), 100);                                 // (the start dialog is gone)
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        const LayoutRect view = f.app.layout().view();
        const int32_t map_w = static_cast<int32_t>(cols.size()) * 32, map_h = static_cast<int32_t>(rows.size()) * 32;
        const LayoutRect map = f.app.renderer().map_view_rect(static_cast<uint32_t>(cols.size()), static_cast<uint32_t>(rows.size()));
        check(map.w == std::min(map_w, view.w) && map.h == std::min(map_h, view.h), "the " + name + ": the map covers " + std::to_string(map.w) + " x " + std::to_string(map.h) + " of the view");
        int64_t stray = 0, covered_dark = 0;
        for (int32_t y = view.y; y < view.bottom(); ++y) {
            for (int32_t x = view.x; x < view.right(); ++x) {
                const bool black = pixel(px, 960, x, y) == std::array<uint8_t, 3>{0, 0, 0};
                if (!map.contains(x, y) && !black) ++stray;
                if (map.contains(x, y) && black) ++covered_dark;
            }
        }
        check(stray == 0, "the " + name + ": the black around the map stays black (" + std::to_string(stray) + " pixels drawn on it)");
        check(covered_dark < map.w * map.h / 4, "the " + name + ": the map itself is drawn");
        std::error_code ignore;
        std::filesystem::remove(level, ignore);
    }
}

void test_margin_and_pages_in_match() {
    group("margin", "no screen of this canvas is a page over a margin of clay: the quick help is the whole canvas (the wide frame at its edge, flat clay beside the page); in a match the options window and the quick help sit over the map view and the frame is not clay");
    const std::array<uint8_t, 3> clay{219, 75, 19};
    {   // the quick help at the start: its own wide page (page_layout.hpp), no margin of clay around a page any more (this block used to pin the clay margin around the centred page)
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); return; }
        f.app.finish_loading();
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        check(f.app.state() == AppState::QuickHelp && f.app.picture() == (LayoutRect{0, 0, 960, 540}), "the quick help is the whole canvas");
        bool frame = true;
        for (const auto& p : {std::pair<int, int>{2, 2}, {957, 2}, {2, 537}, {957, 537}, {480, 2}, {2, 270}, {957, 270}, {600, 537}}) frame = frame && pixel(px, 960, p.first, p.second) != clay;
        check(frame, "the quick help: the wide frame is at the canvas's corners and edges (no margin of clay around a page)");
        bool flat = true;
        for (const auto& p : {std::pair<int, int>{100, 270}, {60, 100}, {880, 100}, {880, 300}, {300, 25}, {600, 520}}) flat = flat && pixel(px, 960, p.first, p.second) == clay;
        check(flat, "... the clay beside and around the page is flat (219, 75, 19), left, right, above and below it");
        check(pixel(px, 960, 200, 130) != clay && pixel(px, 960, 600, 300) != clay, "... and the page's own art (the two columns, centred) is on it");
    }
    {   // the setup screen of this canvas is its wide version: no clay margin around a page, the frame of the screen is at the canvas's edge
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); return; }
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        check(f.app.picture() == (LayoutRect{0, 0, 960, 540}) && pixel(px, 960, 2, 2) != clay && pixel(px, 960, 957, 537) != clay, "the setup screen: the wide screen's own frame is at the canvas's corners (no margin of clay around a page)");
    }
    {
        AppFixture f(Aspect::Wide16x9, true, 960, 540);
        if (!f.ok) { check(false, "the application starts a match"); return; }
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        std::vector<uint8_t> px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 400, 5) != clay && pixel(px, 960, 900, 300) != clay, "the match: the frame fills the canvas, no clay at the corners and the sides");
        // the options window of the original over the match: its card is centred over the map view and the HUD is around it (no clay margin)
        f.app.hud().open_options();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 400, 5) != clay && pixel(px, 960, 940, 300) != clay && pixel(px, 960, 5, 535) != clay && pixel(px, 960, 400, 535) != clay,
              "the options window over a match: the frame is still there around it (the top bar, the left strip, the right panel, the bottom strip)");
        check(pixel(px, 960, 186, 70) == clay && pixel(px, 960, 608, 70) == clay && pixel(px, 960, 190, 70) == clay && pixel(px, 960, 604, 70) == clay,
              "... the card's orange is at the same distance from the view's edges on both sides: (186, 70) and (608, 70), (190, 70) and (604, 70) (the view's middle is x = 397)");
        check(pixel(px, 960, 30, 300) != clay && pixel(px, 960, 30, 100) != clay && pixel(px, 960, 140, 100) != clay, "... and the map shows left of it (the window is no margin)");
        // the quick help
        f.app.hud().close_options();
        f.app.hud().open_quick_help();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 940, 300) != clay && pixel(px, 960, 400, 535) != clay, "the quick help over a match: the frame is around it too");
        f.app.hud().close_quick_help();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay, "closed again: the match's frame is back");
        // the dialog is over the match, not over a clay margin
        f.app.hud().open_quit_dialog();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 900, 300) != clay, "the quit dialog: the match is around it, no margin");
    }
}

namespace {

/// A match with the options window shut and then open, drawn by the real application with the HUD's clock and the animations held (the caret and the pedestals read the one, the ants the other)
struct OptionsPair {
    std::vector<uint8_t> shut, open;
    int32_t width{0}, height{0};
    LayoutPoint pointer;                                    // where the cursor is drawn (in the canvas)
    LayoutRect options_button;                              // the top bar's Options button, which shows its pressed art while the window is open
    bool ok{false};
};

OptionsPair options_pair(Aspect aspect, int32_t width, int32_t height) {
    OptionsPair r;
    AppFixture f(aspect, true, width, height);
    if (!f.ok) return r;
    g_clock = 0;
    f.app.hud().set_ticks_function(&clock_fn);
    f.app.renderer().pin_animation_clock(1500);
    f.app.render_frame();
    r.shut = f.canvas_pixels();
    f.app.hud().open_options();
    f.app.render_frame();
    r.open = f.canvas_pixels();
    r.width = width;
    r.height = height;
    r.pointer = LayoutPoint{f.app.mouse_screen_x() + f.app.picture().x, f.app.mouse_screen_y() + f.app.picture().y};
    const UIButton& button = f.app.hud().options_button();
    r.options_button = LayoutRect{button.x, button.y, button.w, button.h};
    r.ok = true;
    return r;
}

/// What the options window does outside its card: every second pixel is the dither's own color (the pixels where x + y of the window's own numbers is odd), every other pixel is what it was
/// with the window shut; returns the number of pixels that break that rule. Not looked at: the cursor's area, the corner's plate of the frame rate, the Options button (pressed art while the
/// window is open), the card's last column (the art of its right edge, with the dither over every second pixel of it) and, in the original's own 640 x 480 picture, the last column of its
/// bottom right strip (the original's pieces there are 100 wide at x = 459 and 539, so column 639 is the one that its own dim leaves out below y = 400)
int64_t dim_breaks(const OptionsPair& p, const LayoutPoint& offset, const LayoutRect& card, const std::array<uint8_t, 3>& dither, int64_t* dimmed, int64_t* kept) {
    int64_t breaks = 0;
    *dimmed = 0;
    *kept = 0;
    for (int32_t y = 0; y < p.height; ++y) {
        for (int32_t x = 0; x < p.width; ++x) {
            if (card.contains(x, y)) continue;
            if (std::abs(x - p.pointer.x) < 40 && std::abs(y - p.pointer.y) < 40) continue;
            if (x >= p.width - 150 && y >= p.height - 30) continue;
            if (x >= p.options_button.x - 4 && x < p.options_button.x + p.options_button.w + 4 && y >= p.options_button.y - 4 && y < p.options_button.y + p.options_button.h + 4) continue;
            if (p.width == 640 && x == 639 && y >= 400) continue;
            const bool odd = (((x - offset.x) + (y - offset.y)) & 1) != 0;
            const std::array<uint8_t, 3> now = pixel(p.open, p.width, x, y);
            const std::array<uint8_t, 3> was = pixel(p.shut, p.width, x, y);
            if (odd) { ++*dimmed; if (now != dither) ++breaks; }
            else if (x == card.x + card.w && y >= card.y && y < card.y + card.h) continue;
            else { ++*kept; if (now != was) ++breaks; }
        }
    }
    return breaks;
}

}  // namespace

void test_options_dim(const assets::AssetArchive& arc) {
    group("options-dim", "the options window dims everything of the picture outside its card in the original's checker: the classic picture is the oracle for what is dimmed, the 16:9 picture dims the same around the card in the middle of its map view");
    const std::array<uint8_t, 4> d4 = rgb_of(arc, "dith200.bmp", 175);
    const std::array<uint8_t, 3> dither{d4[0], d4[1], d4[2]};
    {   // the original's own picture: the dim is the whole picture except the 442 x 440 card at (17, 20): the ring around it and the whole panel (this is what the 16:9 picture repeats)
        const OptionsPair p = options_pair(Aspect::Classic4x3, 640, 480);
        check(p.ok, "the classic application runs a match with the options window");
        if (p.ok) {
            const ScreenLayout classic = ScreenLayout::classic();
            check(classic.options_card() == (LayoutRect{17, 20, 442, 440}), "classic: the card is 442 x 440 at (17, 20)");
            int64_t dimmed = 0, kept = 0;
            const int64_t breaks = dim_breaks(p, LayoutPoint{}, classic.options_card(), dither, &dimmed, &kept);
            check(breaks == 0 && dimmed > 50000 && kept > 50000, "classic: outside the card every pixel with x + y odd is the dither's color and every other is the shut picture's (" + std::to_string(breaks) + " breaks in " + std::to_string(dimmed + kept) + " pixels)");
        }
    }
    {   // 16:9: the card is centred in the map view (offset (160, 30)), the picture around it is dimmed the same way: the map left and right of it, above and below it, the frame, the panel
        const OptionsPair p = options_pair(Aspect::Wide16x9, 960, 540);
        check(p.ok, "the 16:9 application runs a match with the options window");
        if (p.ok) {
            const ScreenLayout wide = ScreenLayout::with_size(960, 540);
            check(wide.options_card() == (LayoutRect{177, 50, 442, 440}), "16:9: the card is 442 x 440 at (177, 50), in the middle of the map view");
            int64_t dimmed = 0, kept = 0;
            const int64_t breaks = dim_breaks(p, wide.options_offset(), wide.options_card(), dither, &dimmed, &kept);
            check(breaks == 0 && dimmed > 150000 && kept > 150000, "16:9: outside the card every pixel with x + y odd is the dither's color and every other is the shut picture's (" + std::to_string(breaks) + " breaks in " + std::to_string(dimmed + kept) + " pixels)");
            // the places that were not dimmed before the fix: the map left of the card (x 16 - 176), and the panel
            check(pixel(p.open, 960, 100, 201) == dither && pixel(p.open, 960, 100, 200) == pixel(p.shut, 960, 100, 200), "the map left of the card is dimmed (141 pixels of the view were not)");
            check(pixel(p.open, 960, 880, 301) == dither && pixel(p.open, 960, 20, 31) == dither, "the right panel and the frame's left edge are dimmed as in the original's picture");
        }
    }
    {   // what the HUD draws: the original's own picture needs no dim of its own (the window's pieces do it), a bigger one dims four disjoint bands that are the picture minus the card,
        // each band's tiles under its own clip, before the window and with the origin at the picture's
        const ScreenLayout layouts[] = {ScreenLayout::classic(), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1280, 720)};
        for (const ScreenLayout& layout : layouts) {
            HudRig rig(arc, layout);
            rig.hud.open_options();
            const Spy spy = rig.frame();
            const std::string at = std::to_string(layout.width) + " x " + std::to_string(layout.height) + ": ";
            std::vector<LayoutRect> bands;
            size_t tiles = 0, outside = 0;
            for (size_t i = 0; i < spy.events.size(); ++i) {
                if (spy.events[i].kind != Spy::Kind::Clip || spy.events[i].ox != 0 || spy.events[i].oy != 0) continue;
                const LayoutRect band{spy.events[i].x, spy.events[i].y, spy.events[i].w, spy.events[i].h};
                size_t n = 0;
                for (size_t j = i + 1; j < spy.events.size() && spy.events[j].kind != Spy::Kind::ClearClip; ++j) {
                    if (spy.events[j].kind == Spy::Kind::Named && spy.events[j].name == "dith200.bmp") {
                        ++n;
                        if (spy.events[j].x >= band.x + band.w || spy.events[j].x + 200 <= band.x || spy.events[j].y >= band.y + band.h || spy.events[j].y + 200 <= band.y) ++outside;
                    }
                }
                if (n > 0) { bands.push_back(band); tiles += n; }
            }
            if (layout.is_classic()) {
                check(bands.empty() && tiles == 0, at + "the original's own picture draws no dim of its own (the window's pieces are it)");
                continue;
            }
            const LayoutRect card = layout.options_card();
            int64_t area = 0;
            bool disjoint = true, clear_of_card = true;
            for (size_t i = 0; i < bands.size(); ++i) {
                area += static_cast<int64_t>(bands[i].w) * bands[i].h;
                const LayoutRect& b = bands[i];
                if (b.x < card.x + card.w && b.x + b.w > card.x && b.y < card.y + card.h && b.y + b.h > card.y) clear_of_card = false;
                for (size_t j = i + 1; j < bands.size(); ++j) {
                    const LayoutRect& c = bands[j];
                    if (b.x < c.x + c.w && b.x + b.w > c.x && b.y < c.y + c.h && b.y + b.h > c.y) disjoint = false;
                }
            }
            check(bands.size() == 4 && disjoint && clear_of_card, at + "four bands, disjoint, none over the card");
            check(area == static_cast<int64_t>(layout.width) * layout.height - static_cast<int64_t>(card.w) * card.h, at + "the bands are the whole picture minus the card (" + std::to_string(area) + " pixels)");
            check(tiles >= 4 && outside == 0, at + "every tile reaches into its band (" + std::to_string(tiles) + " tiles)");
        }
    }
    {   // another size (a layout that is not the 16:9 one): the dim follows the card's offset, its parity is the window's own
        const ScreenLayout other = ScreenLayout::with_size(961, 541);
        check(other.options_card().x == ScreenLayout::kOptionsCardX + other.options_offset().x && other.options_card().y == ScreenLayout::kOptionsCardY + other.options_offset().y, "the card is the window's own rectangle moved by the options offset at any size");
    }
}

void test_default_and_options() {
    group("default", "16:9 is the default of a desktop game (the command line and the settings give the classic picture), a config made by hand stays classic, the web build's default is 16:9 too");
    auto parse = [](std::initializer_list<const char*> args) {
        std::vector<std::string> storage(args.begin(), args.end());
        std::vector<char*> argv;
        for (std::string& s : storage) argv.push_back(s.data());
        return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
    };
    check(kWebDefaultAspect == Aspect::Wide16x9, "the web build's fallback default is 16:9 (its page always gives the game --aspect: the address, the stored pick or the shape nearest the screen)");
    check(kDesktopDefaultAspect == Aspect::Wide16x9, "a desktop's default is 16:9");
    check(kPlatformDefaultAspect == (
#if defined(__EMSCRIPTEN__)
              kWebDefaultAspect
#else
              kDesktopDefaultAspect
#endif
              ), "this build takes its platform's default");
    ApplicationConfig c = parse({"ants"});
    check(c.aspect == kPlatformDefaultAspect && !c.aspect_given && c.startup_error.empty(), "the command line without the option gives the platform's default and does not say it was given");
    c = parse({"ants", "--aspect", "4:3"});
    check(c.aspect == Aspect::Classic4x3 && c.aspect_given, "--aspect 4:3 gives the classic picture");
    c = parse({"ants", "--aspect", "16:9"});
    check(c.aspect == Aspect::Wide16x9 && c.aspect_given, "--aspect 16:9 gives the wide one");
    c = parse({"ants", "--aspect", "3:2"});
    check(c.aspect == kPlatformDefaultAspect && !c.aspect_given && !c.startup_error.empty(), "a refused --aspect leaves the default and refuses the game");
    check(ApplicationConfig{}.aspect == Aspect::Classic4x3 && !ApplicationConfig{}.aspect_given, "a config that is made by hand (the tests') is the original's 4:3");
    // the application: nothing said, the command line's default, the settings key, the command line beats the settings
    auto aspect_of = [](const ApplicationConfig& cfg, const std::string& settings_text) {
        std::filesystem::path path = temp_ini("ants_wide_default");
        std::error_code ignore;
        std::filesystem::remove(path, ignore);
        if (!settings_text.empty()) {
            std::ofstream out(path);
            out << settings_text;
        }
        ApplicationConfig run = cfg;
        run.headless = true;
        run.start_in_map_select = true;
        run.settings_path = path.string();
        QuietStdout quiet;
        Application app;
        Aspect result = Aspect::Classic4x3;
        ScreenLayout layout;
        if (app.init(run)) {
            result = app.aspect();
            layout = app.layout();
        }
        app.shutdown();
        std::filesystem::remove(path, ignore);
        return std::make_pair(result, layout.width);
    };
    check(aspect_of(parse({"ants"}), "").first == kPlatformDefaultAspect && aspect_of(parse({"ants"}), "").second == (kPlatformDefaultAspect == Aspect::Wide16x9 ? 960 : 640), "a game started with no option runs in the platform's default aspect, with its layout");
    check(aspect_of(parse({"ants", "--aspect", "4:3"}), "").second == 640, "--aspect 4:3: the classic layout");
#if !defined(__EMSCRIPTEN__)
    check(aspect_of(parse({"ants"}), "aspect=4:3\n").first == Aspect::Classic4x3, "the settings' key aspect=4:3 beats the default");
    check(aspect_of(parse({"ants"}), "aspect=16:9\n").first == Aspect::Wide16x9, "aspect=16:9 in the settings: 16:9");
    check(aspect_of(parse({"ants", "--aspect", "16:9"}), "aspect=4:3\n").first == Aspect::Wide16x9, "--aspect beats the settings' key");
    check(aspect_of(parse({"ants"}), "aspect=3:2\n").first == kPlatformDefaultAspect, "a value that is no shape is ignored: the default");
    check(aspect_of(ApplicationConfig{}, "").first == Aspect::Classic4x3, "a config made by hand is 4:3 whatever the platform's default");
#endif
    // the four-window rig: a game that is started with --grid and no aspect (what start_game.sh does) takes its cell's largest rectangle of the default shape
    {
        ApplicationConfig cfg = parse({"ants", "--grid", "2x2", "--cell", "1"});
        std::filesystem::path path = temp_ini("ants_wide_grid");
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.settings_path = path.string();
        QuietStdout quiet;
        Application app;
        if (app.init(cfg)) {
            const WindowRect r = app.window_rect();
            const bool wide_default = kPlatformDefaultAspect == Aspect::Wide16x9;
            check(r.w > 0 && (wide_default ? std::abs(r.h * 16 - r.w * 9) <= 16 : std::abs(r.h * 4 - r.w * 3) <= 4), "a window of the grid (--grid 2x2 --cell 1) has the shape of the default aspect: " + std::to_string(r.w) + " x " + std::to_string(r.h));
        } else {
            check(false, "a game in a grid starts");
        }
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(path, ignore);
    }
}

void test_pointer_edges_in_match() {
    group("match-pointer", "in a 16:9 match the pointer is the picture's whole 960 x 540: the east strip at the right edge scrolls, a pointer beyond a bar of the window is held on the edge");
    AppFixture f(Aspect::Wide16x9, true, 1920, 1200);                  // a 16:10 window: bars above and below the picture (scale 2: 1920 x 1080 of 1200)
    check(f.ok && f.window != nullptr, "the 16:9 application runs a match in a 16:10 window");
    if (!f.ok || f.window == nullptr) return;
    check_rect(f.app.picture(), LayoutRect{0, 0, 960, 540}, "the picture is the whole canvas");
    f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height());
    const int32_t x0 = f.app.renderer().camera().world_x;
    f.motion_at_canvas(955.5f, 270.5f);
    f.deliver();
    check(f.app.mouse_screen_x() == 955 && f.app.mouse_screen_y() == 270, "a pointer at (955, 270) of the canvas is there");
    for (int i = 0; i < 12; ++i) f.app.handle_camera_panning(0.020f);
    check(f.app.renderer().camera().world_x > x0, "the east strip at the right edge of the picture scrolls the view");
    // over the bar above the picture (the window is 1200 high: 60 rows above and below): the nearest edge pixel, and the map scrolls north
    f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height());
    const int32_t y0 = f.app.renderer().camera().world_y;
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = SDL_GetWindowID(f.window);
    e.motion.x = 960;
    e.motion.y = 10;                                                  // inside the black bar above the picture (the picture starts at y = 60)
    SDL_PushEvent(&e);
    f.deliver();
    check(f.app.mouse_screen_y() == 0 && f.app.mouse_screen_x() == 480 && !f.app.pointer_outside(), "the pointer over the top bar of a 16:10 window is the picture's top edge pixel, level with it");
    for (int i = 0; i < 12; ++i) f.app.handle_camera_panning(0.020f);
    check(f.app.renderer().camera().world_y < y0, "... and the map scrolls north");
}
