// The minimap of the match screen (Ants.exe FUN_01009596 and friends): the terrain, the objects, the ants' dots, the frame of the view and, in a replay, the ring of the busiest place (hud.hpp set_camera_ring).
#include "ants_app/hud.hpp"
#include "ants_app/minimap_tables.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/prng.hpp"
#include "ants_app/renderer.hpp"

#include <algorithm>
#include <cmath>

namespace ants::app {

void HUD::render_radar(IRenderer& renderer, const assets::AssetArchive& archive,
                       const sim::WorldState& world, const ViewportCamera& camera) {
    // The minimap frame (x599y35) is part of the uishell composite. The map slot is 119x91 at (480,35); non-square maps letterbox inside it with an adapted bevel.
    const LayoutRect full = layout_.minimap();
    if (world.width == 0 || world.height == 0 || world.cells.size() != static_cast<size_t>(world.width) * world.height) {
        renderer.fill_rect(full.x, full.y, full.w, full.h, {0, 0, 0, 255});
        return;
    }
    const int32_t map_w = static_cast<int32_t>(world.width);
    const int32_t map_h = static_cast<int32_t>(world.height);
    const LayoutRect mini = layout_.minimap_for_map(map_w, map_h);
    const int32_t rx = mini.x, ry = mini.y;
    const int32_t rw = mini.w, rh = mini.h;

    if (mini != full) {
        const int32_t s_top = archive.find_sprite_id("x458y22.bmp");
        const int32_t s_left = archive.find_sprite_id("x458y35.bmp");
        const int32_t s_right = archive.find_sprite_id("x599y35.bmp");
        const int32_t s_card = archive.find_sprite_id("x480y126.bmp");
        if (s_card >= 0) {
            const uint32_t card = static_cast<uint32_t>(s_card);
            renderer.draw_sprite_region(card, full.x - 8, full.y - 5, 8, 5, 0, 6, 8, 5);
            renderer.draw_sprite_region(card, full.x, full.y - 5, 124, 5, 0, 6, 124, 5);
            renderer.draw_sprite_region(card, full.x - 8, full.y, 8, 97, 0, 6, 8, 97);
            renderer.draw_sprite_region(card, full.x, full.y, 124, 97, 0, 6, 124, 97);
        }
        if (s_top >= 0) {
            const uint32_t top = static_cast<uint32_t>(s_top);
            renderer.draw_sprite_region(top, rx - 8, ry - 5, 8 + rw, 5, 14, 8, 8 + rw, 5);
            renderer.draw_sprite_region(top, mini.right(), ry - 5, 5, 5, 141, 8, 5, 5);
        }
        if (s_left >= 0) {
            const uint32_t left = static_cast<uint32_t>(s_left);
            renderer.draw_sprite_region(left, rx - 8, ry, 8, rh, 14, 0, 8, rh);
            renderer.draw_sprite_region(left, rx - 8, mini.bottom(), 8, 6, 14, 91, 8, 6);
        }
        if (s_right >= 0) {
            renderer.draw_sprite_region(static_cast<uint32_t>(s_right), mini.right(), ry, 5, rh, 0, 0, 5, rh);
        }
        if (s_card >= 0) {
            const uint32_t card = static_cast<uint32_t>(s_card);
            renderer.draw_sprite_region(card, rx, mini.bottom(), rw, 6, 0, 0, rw, 6);
            renderer.draw_sprite_region(card, mini.right(), mini.bottom(), 5, 6, 119, 0, 5, 6);
        }
    }

    // Terrain speckle: one random pick per pixel from the class colors, made once per map (the original paints it when
    // cells are first drawn and keeps it until they are repainted)
    if (radar_map_w_ != world.width || radar_map_h_ != world.height || radar_terrain_.size() != static_cast<size_t>(rw * rh)) {
        radar_map_w_ = world.width;
        radar_map_h_ = world.height;
        radar_terrain_.assign(static_cast<size_t>(rw * rh), 0);
        sim::PRNG rng(world.width * 1000u + world.height);
        for (int32_t y = 0; y < rh; ++y) {
            for (int32_t x = 0; x < rw; ++x) {
                const int32_t cx = std::min<int32_t>(map_w - 1, x * map_w / rw);
                const int32_t cy = std::min<int32_t>(map_h - 1, y * map_h / rh);
                const uint8_t cls = minimap_class(world.cells[static_cast<size_t>(cy) * world.width + static_cast<size_t>(cx)]);
                radar_terrain_[static_cast<size_t>(y * rw + x)] = kMinimapClassColors[cls < 6 ? cls : 5][rng.rand() % 5u];
            }
        }
    }

    const auto& palette = archive.get_palette();
    const bool fog = world.fog_of_war_enabled;

    // FUN_01009596: every pixel takes the cell under it. An object shows its color from the table (a tile id without a record: color 0) unless the cell is empty or holds a bomb;
    // then the terrain shows (speckle, or the fog color of an unexplored cell). In fog the objects that are removed or given out (power-ups, food, fire walls) fall back to the
    // terrain while their cell is unexplored; every other object, rocks, toys, bridges and the hills of every colony, keeps its color in the fog
    std::vector<uint8_t> pixels(static_cast<size_t>(rw * rh), 0);   // palette indices
    for (int32_t y = 0; y < rh; ++y) {
        const int32_t cy = std::min<int32_t>(map_h - 1, y * map_h / rh);
        for (int32_t x = 0; x < rw; ++x) {
            const int32_t cx = std::min<int32_t>(map_w - 1, x * map_w / rw);
            const auto& cell = world.cells[static_cast<size_t>(cy) * world.width + static_cast<size_t>(cx)];
            const bool explored = !fog || world.is_tile_revealed(cx, cy);
            const uint16_t id = minimap_object_id(cell);
            bool object = id != kMinimapNoObject && !minimap_is_bomb(id);
            if (object && !explored) {
                const uint8_t flags = sim::movement::tile_flags_of(id);
                object = (flags & (sim::movement::kTileFlagFood | sim::movement::kTileFlagPowerUp)) == 0 && id != sim::TILE_FIREWALL;
            }
            uint8_t color;
            if (object) {
                color = static_cast<uint8_t>(minimap_object_entry(id) & 0xffu);
            } else if (explored) {
                color = radar_terrain_[static_cast<size_t>(y * rw + x)];
            } else {
                const uint8_t cls = minimap_class(cell);
                color = kMinimapFogColors[cls < 8 ? cls : 0];
            }
            pixels[static_cast<size_t>(y * rw + x)] = color;
        }
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(rw * rh) * 4u, 255);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const auto& c = palette[pixels[i]];
        rgba[i * 4 + 0] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
    }
    renderer.draw_rgba_image(rx, ry, rw, rh, rgba.data());

    // The scale of the image: world pixels per image pixel are (map * 32) / 119 across and / 91 down (FUN_0100925b). A world point or length divided by it is truncated
    const int64_t world_w_px = static_cast<int64_t>(map_w) * 32;
    const int64_t world_h_px = static_cast<int64_t>(map_h) * 32;
    auto to_image_x = [&](int64_t world_px) { return static_cast<int32_t>(world_px * rw / world_w_px); };
    auto to_image_y = [&](int64_t world_px) { return static_cast<int32_t>(world_px * rh / world_h_px); };

    // FUN_01009988 paints the dots of the world's object list over the image, in list order and in screen coordinates (a dot is not clipped to the image): plants first (made with
    // the match screen), then the ants in the order they were made. A dot is a filled rectangle centred on the object's position (FUN_01009899): the size flag of its record
    // (one or two cells, 32 world pixels each) scaled down and truncated, at least `min_size`; the top edge is taken from half the WIDTH, as the original does
    auto dot = [&](int32_t world_x, int32_t world_y, uint8_t color, int32_t size_flag, int32_t min_size) {
        const int32_t w = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(32) * size_flag * rw / world_w_px), min_size);
        const int32_t h = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(32) * size_flag * rh / world_h_px), min_size);
        const int32_t cx = to_image_x(world_x);
        const int32_t cy = to_image_y(world_y);
        int32_t left, top, right, bottom;
        if (w > 1 && h > 1) {
            left = cx - (w >> 1);
            top = cy - (w >> 1);
            right = cx + (w - (w >> 1));
            bottom = cy + (h - (h >> 1));
        } else {
            left = std::min(cx, rw - 1);
            top = std::min(cy, rh - 1);
            right = left + w;
            bottom = top + h;
        }
        if (right > left && bottom > top) {
            const auto& c = palette[color];
            renderer.fill_rect(rx + left, ry + top, right - left, bottom - top, {c.r, c.g, c.b, 255});
        }
    };
    for (const auto& plant : world.plants) {
        const uint16_t entry = minimap_object_entry(plant.tile_id);
        const int32_t size_flag = entry >> 8;
        if (size_flag > 0) dot(plant.x * 32 + 16, plant.y * 32 + 16, static_cast<uint8_t>(entry & 0xffu), size_flag, 0);
    }
    // In fog an ant shows when it is the viewer's own, an ally's, or stands on an explored cell (FUN_0101aa0d); all ants have size flag 1 and a minimum size of 2
    const uint8_t ally = local_player_id_ < world.player_alliances.size() ? world.player_alliances[local_player_id_] : uint8_t{255};
    for (const auto& ant : world.ants) {
        if (fog && ant.player_id != local_player_id_ && ant.player_id != ally && !world.is_tile_revealed(ant.px / 32, ant.py / 32)) continue;
        dot(ant.px, ant.py, kMinimapAntColors[ant.player_id % 4], 1, 2);
    }

    // The view frame (0x1009ae2 - 0x1009b6f, drawn last with GDI FrameRect in (251, 251, 255)): the size of the view scaled down plus one, at the view's origin scaled down, moved
    // inside the image when it would end beyond its right or bottom edge
    // (A view bigger than the map on an axis shows all of it there: the frame is the image's whole width / height then. The original's view is smaller than its smallest map;
    // the 16:9 view of 762 x 500 is bigger than a 16 x 16 map.)
    // (At a zoom the view shows view / zoom world pixels: the frame is the world that is seen, 442 x 440 or 762 x 500 at the zoom 1, half as much at 2, twice as much at 0.5)
    const int32_t seen_w = zoom::visible(layout_.view().w, camera.zoom);
    const int32_t seen_h = zoom::visible(layout_.view().h, camera.zoom);
    int32_t fl, ft, fr, fb;
    if (world_w_px <= seen_w) {
        fl = rx;
        fr = rx + rw;
    } else {
        const int32_t frame_w = to_image_x(seen_w) + 1;
        fl = rx + to_image_x(static_cast<int32_t>(camera.x));
        fr = fl + frame_w;
        if (fr >= rx + rw) { fr = rx + rw; fl = fr - frame_w; }
    }
    if (world_h_px <= seen_h) {
        ft = ry;
        fb = ry + rh;
    } else {
        const int32_t frame_h = to_image_y(seen_h) + 1;
        ft = ry + to_image_y(static_cast<int32_t>(camera.y));
        fb = ft + frame_h;
        if (fb >= ry + rh) { fb = ry + rh; ft = fb - frame_h; }
    }
    renderer.draw_rect(fl, ft, fr - fl, fb - ft, {251, 251, 255, 255});

    // The ring of the camera's busiest place (a replay, docs/REPLAYS.md "Camera"): white with a dark edge, whole when the camera follows the place and in ten dashes when it is a hint
    if (ring_shown_) {
        const int32_t cx = rx + std::clamp(to_image_x(ring_x_), 0, rw - 1);
        const int32_t cy = ry + std::clamp(to_image_y(ring_y_), 0, rh - 1);
        constexpr double kPi = 3.14159265358979323846;
        for (int32_t dy = -7; dy <= 7; ++dy) {
            for (int32_t dx = -7; dx <= 7; ++dx) {
                const double d = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
                if (d < 3.7 || d > 6.6) continue;
                bool white = d >= 4.5 && d <= 5.6;
                if (white && !ring_solid_) {
                    const double turn = (std::atan2(static_cast<double>(dy), static_cast<double>(dx)) + kPi) / (2.0 * kPi);       // (0 .. 1 round the ring)
                    if ((static_cast<int>(turn * 10.0) & 1) != 0) white = false;                                                   // (a gap shows the dark edge)
                }
                renderer.fill_rect(cx + dx, cy + dy, 1, 1, white ? assets::ColorRGBA{255, 255, 255, 255} : assets::ColorRGBA{18, 14, 20, 255});
            }
        }
    }
}

}  // namespace ants::app
