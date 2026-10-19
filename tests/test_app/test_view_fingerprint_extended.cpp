// View fingerprint, part 6: the maps of an Extended game, 100 x 100 tiles (3,200 pixels a side) and the largest the game plays, 120 columns by 128 rows: the edge scroll, the minimap, the start
// view and the camera over every pixel of the screen, and whole frames of the HUD (with its minimap) at the corners and in the middle. New fingerprints only: the numbers of the 60 x 60 and
// the other Classic sizes are pinned in the other parts and do not move. (See test_view_fingerprint.cpp for what the whole program pins.)
#include "view_fingerprint_fixtures.hpp"

namespace {

std::vector<Cam> cams_of(int32_t tiles_w, int32_t tiles_h) {
    const auto nine = nine_cams(tiles_w, tiles_h);
    return std::vector<Cam>(nine.begin(), nine.end());
}

}  // namespace

void extended_scenarios(const assets::AssetArchive& arc) {
    const struct { int32_t w, h; } sizes[] = {{100, 100}, {120, 128}};
    for (const auto& s : sizes) {
        const std::string edge = "ext.edge." + map_name(s.w, s.h) + ".rate50.all9";
        if (wanted(edge)) record(edge, edge_sweep(s.w, s.h, 50, cams_of(s.w, s.h)));
        const std::string point = "ext.minimap.point." + map_name(s.w, s.h);
        if (wanted(point)) {
            record(point, sweep([&](Fnv64& h, int32_t x, int32_t y) {
                       int32_t wx = 0;
                       int32_t wy = 0;
                       minimap_point(x, y, s.w, s.h, wx, wy);
                       h.i32(wx);
                       h.i32(wy);
                   }));
        }
        const std::string scroll = "ext.minimap.scroll." + map_name(s.w, s.h) + ".all9";
        if (wanted(scroll)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const Cam& c : nine_cams(s.w, s.h)) {
                for (int32_t y = 0; y < kH; ++y) {
                    for (int32_t x = 0; x < kW; ++x) {
                        hash_scroll(h, minimap_scroll_step(x, y, c.x, c.y, s.w, s.h));
                        ++n;
                    }
                }
            }
            record(scroll, Measure{h.value(), n});
        }
        const std::string start = "ext.start_view.origins." + map_name(s.w, s.h);
        if (wanted(start)) {
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t ty = 0; ty < s.h; ++ty) {
                for (int32_t tx = 0; tx < s.w; ++tx) {
                    int32_t ox = 0;
                    int32_t oy = 0;
                    start_view_origin(tx, ty, s.w, s.h, ox, oy);
                    h.i32(ox);
                    h.i32(oy);
                    ++n;
                }
            }
            record(start, Measure{h.value(), n});
        }
        const std::string clamp = "ext.camera.clamp." + map_name(s.w, s.h);
        if (wanted(clamp)) {
            Fnv64 h;
            uint64_t n = 0;
            const float values[] = {-500.0f, -1.0f, 0.0f, 0.5f, 441.0f, 442.0f, 443.0f, 1600.0f, 2757.5f, 2758.0f, 2759.0f, 3199.0f, 3200.0f, 3201.0f, 3500.0f, 3839.0f, 3840.0f, 4096.0f, 9000.0f};
            for (float vx : values) {
                for (float vy : values) {
                    ViewportCamera cam;
                    cam.x = vx;
                    cam.y = vy;
                    cam.clamp_to_bounds(static_cast<uint32_t>(s.w), static_cast<uint32_t>(s.h));
                    h.i32(static_cast<int32_t>(cam.x * 4.0f));
                    h.i32(static_cast<int32_t>(cam.y * 4.0f));
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
            record(clamp, Measure{h.value(), n});
        }
        const std::string center = "ext.camera.center_on." + map_name(s.w, s.h);
        if (wanted(center)) {
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t wy = -64; wy < s.h * 32 + 64; wy += 61) {
                for (int32_t wx = -64; wx < s.w * 32 + 64; wx += 67) {
                    ViewportCamera cam;
                    cam.center_on(wx, wy, static_cast<uint32_t>(s.w), static_cast<uint32_t>(s.h));
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
            record(center, Measure{h.value(), n});
        }
    }
    // the HUD over a 100 x 100 world (the minimap of the whole map in its 119 x 91 pixels, the ants and hills in it), with the view at the corners and in the middle
    for (const Cam& c : nine_cams(100, 100)) {
        Scene s(arc, 0, 100, 100);
        s.look(c.x, c.y);
        s.shoot(std::string("ext.hud.100x100.cam_") + c.name);
    }
}
