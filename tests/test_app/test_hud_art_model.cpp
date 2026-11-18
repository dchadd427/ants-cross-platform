// The HUD art, the model's part, continued (see test_hud_art.cpp): the manifest, the frame at every shape, the nine-slice and the choice of the sheet.
#include "hud_art_fixtures.hpp"

namespace {

using hud_art::Art;
using hud_art::Draw;

// ---- a small valid manifest to break in one place at a time --------------------------------------------------------------------------------------------------------------------

std::vector<std::string> small_manifest() {
    return {"ants-hud-art 1",
            "canvas 960 540",
            "sheetsize 1000 800",
            "sheet 1 a.png",
            "sheet 2 b.png",
            "sheet 3 c.png",
            "sprite top_bar 960 22 0 0 0 0 stretch top x 140 140 332 320",
            "sprite left_strip 17 618 0 30 0 22 left stretch y 270 270 398 120",
            "sprite bottom_strip 943 19 0 60 17 521 stretch bottom x 0 0 192 206",
            "sprite lower_panel 182 353 0 90 778 268 right stretch y 92 92 220 104",
            "sprite panel_top 182 8 0 120 778 22 right top",
            "sprite minimap_frame 182 106 0 130 778 30 right top",
            "sprite card 182 116 0 240 778 136 right top",
            "sprite status_box 182 16 0 360 778 252 right top",
            "sprite thing 10 10 200 0 800 100 right bottom",
            "anim one",
            "frame 100 thing 800 100 card 778 136"};
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

/// The manifest with the line that starts with `prefix` replaced by `with` ("" removes it)
std::string changed(const std::string& prefix, const std::string& with) {
    std::vector<std::string> lines = small_manifest();
    for (std::string& l : lines) {
        if (l.compare(0, prefix.size(), prefix) == 0) {
            l = with;
            break;
        }
    }
    lines.erase(std::remove(lines.begin(), lines.end(), std::string()), lines.end());
    return joined(lines);
}

void refused(const std::string& text, const std::string& what) {
    Art art;
    check(!art.parse(text) && !art.loaded() && !art.what().empty() && art.sprites().empty(), "refused: " + what);
}

/// The names of the animations that the game plays through the HUD art: the buttons, the pedestals of every kind with every transition, the tray's eggs, the rim flash, the lunch box
std::vector<std::string> played_animations() {
    std::vector<std::string> names = {"buthlpu", "buthlpr", "buthlpd", "butoptu", "butoptr", "butoptd", "butqitu", "butqitr", "butqitd", "butallu", "butallr", "butalld", "butalsu", "butalsr", "butalsd",
                                      "butcanu", "butcand", "UI_LBOX", "butdefl", "butdefr"};
    for (int n = 1; n <= 9; ++n) names.push_back("egg" + std::to_string(n));
    for (const PedestalKind kind : {PedestalKind::Move, PedestalKind::Bomb, PedestalKind::Attack, PedestalKind::Fire, PedestalKind::Thief, PedestalKind::Ally, PedestalKind::Swim, PedestalKind::Egg}) {
        for (const std::string& n : {pedestal_up_anim(kind), pedestal_down_anim(kind), pedestal_press_anim(kind), pedestal_swap_out_anim(kind), pedestal_swap_in_anim(kind), pedestal_sink_anim(kind), pedestal_rise_anim(kind)}) names.push_back(n);
    }
    return names;
}

}  // namespace

void test_manifest() {
    group("manifest", "the shipped manifest is read; a manifest that is wrong in any of the ways listed is refused; every animation names a sprite, and every animation the game plays has one");
    {
        Art art;
        check(art.parse(joined(small_manifest())) && art.loaded() && art.find_sprite("thing") >= 0 && art.find_animation("one") != nullptr && art.sheet_file(2) == "b.png", "the small manifest is read");
        check(art.find_sprite("nothing") == -1 && art.find_animation("nothing") == nullptr && art.sprite_at(-1) == nullptr && art.sprite_at(1000) == nullptr, "a name that is not there is none");
        const uint64_t g1 = art.generation();
        Art second;
        check(second.parse(joined(small_manifest())) && second.generation() != g1 && second.generation() != 0, "every manifest that is read has a generation of its own (a renderer's textures follow it)");
    }
    refused("hello\n", "not a manifest");
    refused(changed("ants-hud-art", "ants-hud-art 2"), "another version");
    refused(changed("canvas", "canvas 640 480"), "a canvas that is not 960 x 540");
    refused(changed("sheetsize", ""), "no sheet size");
    refused(changed("sheetsize", "sheetsize 0 300"), "a sheet without a width");
    refused(changed("sheet 3", ""), "no sheet for the scale 3");
    refused(changed("sheet 1", "sheet 4 a.png"), "a sheet of the scale 4");
    refused(changed("sheet 1", "sheet 1 ../a.png"), "a sheet outside the folder");
    refused(changed("sheet 1", "sheet 1 sub/a.png"), "a sheet in a folder");
    refused(changed("sprite card", "sprite card 182 116 0 240 778 136 right"), "a sprite with too few fields");
    refused(changed("sprite card", "sprite card 182 116 0 240 778 136 diagonal top"), "an anchor that does not exist");
    refused(changed("sprite card", "sprite card 182 116 0 240 778 136 right middle"), "a vertical anchor that does not exist");
    refused(changed("sprite card", "sprite card 0 116 0 240 778 136 right top"), "a sprite without a width");
    refused(changed("sprite card", "sprite card 182 116 x 240 778 136 right top"), "a number that is not one");
    refused(changed("sprite card", "sprite card 182 116 900 240 778 136 right top"), "a sprite that ends outside its sheet");
    refused(changed("sprite thing", "sprite card 10 10 200 0 800 100 right bottom"), "a sprite twice");
    refused(changed("sprite top_bar", "sprite top_bar 960 22 0 0 0 0 stretch top z 140 140 332 320"), "a slice along no axis");
    refused(changed("sprite top_bar", "sprite top_bar 960 22 0 0 0 0 stretch top x 140 140 332 900"), "a slice whose start and end are longer than the sprite");
    refused(changed("sprite top_bar", "sprite top_bar 960 22 0 0 0 0 stretch top x 140 332 140 320"), "a slice whose run is backwards");
    refused(changed("sprite top_bar", "sprite top_bar 960 22 0 0 0 0 stretch top x 140 140 961 320"), "a slice whose run ends outside the sprite");
    refused(changed("sprite top_bar", "sprite top_bar 960 22 0 0 0 0 stretch top"), "a long piece of the frame without a slice");
    refused(changed("sprite card", ""), "a frame without its card");
    refused(changed("sprite lower_panel", ""), "a frame without its lower panel");
    refused(changed("sprite status_box", ""), "a frame without its status box");
    refused(changed("frame 100", "frame 100 nothing 1 1"), "a frame that names no sprite");
    refused(changed("frame 100", "frame 0 thing 800 100"), "a frame of no time");
    refused(changed("frame 100", "frame 100 thing 800"), "a part without a place");
    refused(changed("frame 100", ""), "an animation without a frame");
    refused(changed("anim one", "frame 100 thing 800 100"), "a frame before any animation");
    refused(changed("anim one", "anim one\nanim one"), "an animation twice");
    refused(changed("anim one", "shade thing"), "a record that is not known");
    {
        Art art;
        check(!art.load("/nonexistent/folder") && !art.loaded() && art.what().find("hud_art.txt") != std::string::npos, "a folder without the manifest: refused, and it says which file");
    }

    // the manifest that ships
    Art art;
    if (!load_art(art)) {
        check(false, "the shipped manifest is read");
        return;
    }
    check(art.loaded() && art.top_bar >= 0 && art.left_strip >= 0 && art.bottom_strip >= 0 && art.lower_panel >= 0 && art.panel_top >= 0 && art.minimap_frame >= 0 && art.card >= 0 && art.status_box >= 0 && art.score_window >= 0, "the shipped manifest is read, with the frame's eight pieces and the score window");
    for (const hud_art::Sprite& s : art.sprites()) {
        check(s.sheet_x + s.w <= art.sheet_width() && s.sheet_y + s.h <= art.sheet_height(), "the sprite " + s.name + " is inside its sheet");
    }
    check(art.sheet_width() > 0 && art.sheet_height() > 0 && art.sheet_width() * 3 <= 4096 && art.sheet_height() * 3 <= 4096, "the largest sheet fits the 4096 pixels that every graphics card takes");
    const assets::AssetArchive* arc_ptr = nullptr;
    assets::AssetArchive arc;
    if (arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) arc_ptr = &arc;
    check(arc_ptr != nullptr, "the original's archive is read");
    for (const hud_art::Animation& a : art.animations()) {
        check(arc_ptr != nullptr && arc.find_animation(a.name) != nullptr, "the art's animation " + a.name + " is one of the original's (a name that the game knows)");
        for (const hud_art::Frame& f : a.frames) {
            check(f.ms > 0 && !f.parts.empty(), "a frame of " + a.name + " has a time and a piece");
        }
    }
    for (const std::string& name : played_animations()) {
        const hud_art::Animation* a = art.find_animation(name);
        check(a != nullptr && !a->frames.empty(), "the art has the animation " + name + ", which the game plays");
    }
    // the pedestals' own timing (the swap, the rise and the press play the art's frames in the art's times): the original's single-frame animations stay single frames
    for (const std::string& name : {std::string("butmovu"), std::string("butmovd"), std::string("butgone")}) {
        const hud_art::Animation* a = art.find_animation(name);
        check(a != nullptr && a->frames.size() == 1, name + " is one frame");
    }
}

void test_frame() {
    group("frame", "at all seven shapes the frame's pieces tile the picture but the map view, once; every piece of every animation is inside the canvas; the buttons are the hit zones' size and place");
    hud_art::Art art;
    if (!load_art(art)) {
        check(false, "the art is read");
        return;
    }
    for (const Shape& shape : kShapes) {
        const std::string at = std::string(shape.name) + " (" + std::to_string(shape.w) + " x " + std::to_string(shape.h) + "): ";
        const ScreenLayout layout = ScreenLayout::with_size(shape.w, shape.h);
        const std::vector<Draw> draws = hud_art::frame_draws(art, layout);
        std::vector<int> covered(static_cast<size_t>(shape.w) * static_cast<size_t>(shape.h), 0);
        bool inside = true;
        bool exact = true;
        for (const Draw& d : draws) {
            const hud_art::Sprite* s = art.sprite_at(d.sprite);
            inside = inside && s != nullptr && d.dst.x >= 0 && d.dst.y >= 0 && d.dst.right() <= shape.w && d.dst.bottom() <= shape.h;
            exact = exact && s != nullptr && d.src.w == d.dst.w && d.src.h == d.dst.h && d.src.x >= 0 && d.src.y >= 0 && d.src.right() <= s->w && d.src.bottom() <= s->h;
            if (!inside) continue;
            for (int32_t y = d.dst.y; y < d.dst.bottom(); ++y) {
                for (int32_t x = d.dst.x; x < d.dst.right(); ++x) ++covered[static_cast<size_t>(y) * static_cast<size_t>(shape.w) + static_cast<size_t>(x)];
            }
        }
        check(inside, at + "every copy of the frame is inside the canvas");
        check(exact, at + "every copy takes a part of its sprite at its own size (the sheet is drawn 1 to 1)");
        // the hole is the map view without its first row and column (the frame's own black line is there): nothing of the frame is drawn in it, and everything else is covered
        const LayoutRect view = layout.view();
        const LayoutRect hole{view.x + 1, view.y + 1, view.w - 1, view.h - 1};
        int64_t holes = 0, gaps = 0, twice = 0, twice_outside_panel = 0;
        const int32_t panel_x = shape.w - 182;
        for (int32_t y = 0; y < shape.h; ++y) {
            for (int32_t x = 0; x < shape.w; ++x) {
                const int c = covered[static_cast<size_t>(y) * static_cast<size_t>(shape.w) + static_cast<size_t>(x)];
                if (hole.contains(x, y)) holes += c != 0 ? 1 : 0;
                else gaps += c == 0 ? 1 : 0;
                if (c > 1) {
                    ++twice;
                    if (x < panel_x) ++twice_outside_panel;
                }
            }
        }
        check(holes == 0, at + "nothing of the frame is drawn in the map view (" + std::to_string(holes) + " pixels)");
        check(gaps == 0, at + "everything outside the map view is covered (" + std::to_string(gaps) + " pixels are not)");
        check(twice_outside_panel == 0, at + "no pixel outside the panel is drawn twice");
        check(shape.h > 480 ? twice == 0 : twice > 0, at + (shape.h > 480 ? "no pixel is drawn twice" : "the lower panel of the original's own shape is shorter than its start and end: they overlap"));
        // the panel stands at the right edge, the long pieces reach the edges
        const int32_t card = art.card;
        const Draw* card_draw = nullptr;
        for (const Draw& d : draws) {
            if (d.sprite == card) card_draw = &d;
        }
        check(card_draw != nullptr && card_draw->dst.x == shape.w - 182, at + "the card is 182 pixels from the right edge");
        // every piece of every animation, in every frame
        bool anim_inside = true;
        std::string where;
        for (const hud_art::Animation& a : art.animations()) {
            for (size_t f = 0; f < a.frames.size(); ++f) {
                for (const Draw& d : hud_art::animation_draws(art, a.name, f, layout)) {
                    if (d.dst.x < 0 || d.dst.y < 0 || d.dst.right() > shape.w || d.dst.bottom() > shape.h) {
                        anim_inside = false;
                        where = a.name;
                    }
                }
            }
        }
        check(anim_inside, at + "every piece of every animation is inside the canvas " + where);
        // the buttons: the art's sprite is the size of the hit zone (HUD::apply_layout) and stands on it
        struct Zone {
            const char* animation;
            LayoutRect zone;
        };
        const Zone zones[] = {{"buthlpu", {layout.right(476), 7, 46, 23}}, {"butoptu", {layout.right(525), 7, 52, 23}}, {"butqitu", {layout.right(579), 7, 46, 23}},
                              {"butallu", {layout.right(532), layout.bottom(443), 44, 24}}, {"butalsu", {layout.right(579), layout.bottom(443), 46, 24}}};
        for (const Zone& z : zones) {
            const std::vector<Draw> d = hud_art::animation_draws(art, z.animation, 0, layout);
            check(d.size() == 1 && d[0].dst == z.zone, at + std::string(z.animation) + " is the button's rectangle");
        }
    }
    // the frame at the same width does not depend on the height, and at the same height the right side moves with the width (the panel keeps its size)
    {
        const std::vector<Draw> a = hud_art::frame_draws(art, ScreenLayout::with_size(960, 540));
        const std::vector<Draw> b = hud_art::frame_draws(art, ScreenLayout::with_size(1200, 540));
        int64_t panel_a = 0, panel_b = 0;
        for (const Draw& d : a) panel_a += d.dst.x >= 960 - 182 ? static_cast<int64_t>(d.dst.w) * d.dst.h : 0;
        for (const Draw& d : b) panel_b += d.dst.x >= 1200 - 182 ? static_cast<int64_t>(d.dst.w) * d.dst.h : 0;
        check(panel_a == panel_b, "the panel is the same size at 16:9 and at 20:9");
    }
}

namespace {

/// The source column that a slice puts at the pixel `p` of a piece of `length` pixels: the oracle (the start, the repeated run, the end; the end drawn over the start when the piece is short)
int32_t source_of(int32_t p, int32_t length, int32_t full, const hud_art::Slice& sl) {
    if (p >= length - sl.end) return full - sl.end + (p - (length - sl.end));
    if (p < sl.start) return p;
    return sl.tile_from + (p - sl.start) % (sl.tile_to - sl.tile_from);
}

void check_slices(const Art& art, const char* name, bool horizontal) {
    const int32_t index = art.find_sprite(name);
    const hud_art::Sprite& s = art.sprites()[static_cast<size_t>(index)];
    const int32_t full = horizontal ? s.w : s.h;
    for (const int32_t length : {full, full + 1, full + 191, 640, 2 * full + 7, s.slice.start + s.slice.end, s.slice.start + s.slice.end - 1, s.slice.start + s.slice.end + 1, s.slice.start + 3, s.slice.end + 3, s.slice.start - 1, s.slice.start}) {
        if (length < s.slice.end) continue;
        const std::string at = std::string(name) + " at " + std::to_string(length) + ": ";
        const std::vector<Draw> draws = hud_art::sliced_draws(art, index, 10, 20, length);
        std::vector<int32_t> from(static_cast<size_t>(length), -1);
        bool shaped = !draws.empty();
        for (const Draw& d : draws) {
            const int32_t along = horizontal ? d.dst.w : d.dst.h;
            const int32_t across = horizontal ? d.dst.h : d.dst.w;
            const int32_t at_dst = horizontal ? d.dst.x - 10 : d.dst.y - 20;
            const int32_t at_src = horizontal ? d.src.x : d.src.y;
            shaped = shaped && across == (horizontal ? s.h : s.w) && (horizontal ? d.dst.y == 20 && d.src.y == 0 && d.src.w == along : d.dst.x == 10 && d.src.x == 0 && d.src.h == along) && along > 0 && at_dst >= 0 && at_dst + along <= length &&
                     at_src >= 0 && at_src + along <= full;
            for (int32_t i = 0; shaped && i < along; ++i) from[static_cast<size_t>(at_dst + i)] = at_src + i;      // (a later copy is drawn over an earlier one)
        }
        check(shaped, at + "every copy has the piece's width, a place inside the piece and inside its length");
        bool right = shaped;
        for (int32_t p = 0; right && p < length; ++p) right = from[static_cast<size_t>(p)] == source_of(p, length, full, s.slice);
        check(right, at + "every pixel of the piece comes from the start, the repeated run or the end where the rule says");
    }
}

}  // namespace

void test_slices() {
    group("slices", "a long piece is the kept start, the repeated run (the last copy cut) and the kept end, whatever the length");
    hud_art::Art art;
    if (!load_art(art)) {
        check(false, "the art is read");
        return;
    }
    check_slices(art, "top_bar", true);
    check_slices(art, "bottom_strip", true);
    check_slices(art, "left_strip", false);
    check_slices(art, "lower_panel", false);
    // a sprite without a slice is one copy of itself, whatever the length asked for
    {
        const std::vector<Draw> d = hud_art::sliced_draws(art, art.card, 5, 6, 999);
        const hud_art::Sprite& card = art.sprites()[static_cast<size_t>(art.card)];
        check(d.size() == 1 && d[0].dst == (LayoutRect{5, 6, card.w, card.h}) && d[0].src == (LayoutRect{0, 0, card.w, card.h}), "a piece without a slice is one copy");
        check(hud_art::sliced_draws(art, -1, 0, 0, 10).empty(), "a sprite that is not there gives nothing");
    }
    // the run is repeated at the length that the shape of the picture asks: the top bar of 16:9 and of 32:9
    {
        size_t copies960 = hud_art::frame_draws(art, ScreenLayout::with_size(960, 540)).size();
        size_t copies1920 = hud_art::frame_draws(art, ScreenLayout::with_size(1920, 540)).size();
        check(copies1920 > copies960, "a wider picture repeats the plain run more often");
    }
}

void test_scale() {
    group("scale", "the sheet is the nearest scale at or above the output's, and the renderer takes it");
    const struct {
        double output;
        int32_t sheet;
    } table[] = {{-1.0, 1}, {0.0, 1}, {0.4, 1}, {0.999, 1}, {1.0, 1}, {1.0000001, 1}, {1.001, 2}, {1.35, 2}, {1.5, 2}, {1.999, 2}, {2.0, 2}, {2.0000001, 2}, {2.001, 3}, {2.5, 3}, {2.999, 3}, {3.0, 3}, {3.4, 3}, {8.0, 3}};
    for (const auto& row : table) check(hud_art::Art::scale_for(row.output) == row.sheet, "an output of " + std::to_string(row.output) + " pixels for a canvas pixel draws from the " + std::to_string(row.sheet) + "x sheet");
    check(hud_art::Art::scale_for(std::nan("")) == 1, "a number that is not a number draws from the 1x sheet");

    hud_art::Art art;
    assets::AssetArchive arc;
    if (!load_art(art) || !arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        check(false, "the art and the archive are read");
        return;
    }
    // the renderer takes the output's scale from the window against the canvas (the arithmetic of the screenshot), and makes the texture of that sheet only
    const struct {
        int32_t window_w, window_h;
        int32_t canvas_w, canvas_h;
        int32_t sheet;
    } windows[] = {{960, 540, 960, 540, 1}, {1920, 1080, 960, 540, 2}, {2880, 1620, 960, 540, 3}, {1300, 731, 960, 540, 2}, {3000, 1700, 960, 540, 3}, {640, 360, 960, 540, 1}, {2400, 1080, 1200, 540, 2},
                   {1280, 960, 640, 480, 2}, {1000, 540, 960, 540, 1}};
    for (const auto& w : windows) {
        const QuietStdout quiet;
        ensure_sdl();
        SDL_Window* win = SDL_CreateWindow("hud-art-scale", 0, 0, w.window_w, w.window_h, SDL_WINDOW_HIDDEN);
        Renderer renderer;
        const bool up = win != nullptr && renderer.init(win, arc);
        const std::string at = std::to_string(w.window_w) + " x " + std::to_string(w.window_h) + " window, " + std::to_string(w.canvas_w) + " x " + std::to_string(w.canvas_h) + " canvas: ";
        check(up, at + "the renderer is up");
        if (up) {
            renderer.set_canvas_size(w.canvas_w, w.canvas_h);
            check(renderer.art_scale_now() == w.sheet, at + "the sheet is " + std::to_string(w.sheet) + "x");
            check(renderer.art_textures() == 0, at + "no texture is made before a piece is drawn");
            renderer.begin_frame();
            renderer.draw_art(art, art.card, 0, 0, 182, 116, 0, 0, 182, 116);
            check(renderer.art_textures() == 1, at + "drawing a piece makes one texture, of the one sheet it needs");
            if (w.canvas_w * w.sheet == w.window_w && w.canvas_h * w.sheet == w.window_h) {
                // an exact scale: the piece is the sheet's own pixels, copied 1 to 1
                DecodedImage sheet;
                const hud_art::Sprite& s = art.sprites()[static_cast<size_t>(art.card)];
                if (art.sheet_pixels(w.sheet, sheet)) {
                    std::vector<uint8_t> px(static_cast<size_t>(w.window_w) * static_cast<size_t>(w.window_h) * 4u, 0);
                    SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w.window_w * 4);
                    int64_t bad = 0;
                    for (int32_t y = 0; y < s.h * w.sheet; ++y) {
                        for (int32_t x = 0; x < s.w * w.sheet; ++x) {
                            const size_t from = (static_cast<size_t>(s.sheet_y * w.sheet + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(s.sheet_x * w.sheet + x)) * 4;
                            const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(w.window_w) + static_cast<size_t>(x)) * 4;
                            for (size_t c = 0; c < 3; ++c) bad += px[to + c] != sheet.rgba[from + c] ? 1 : 0;
                        }
                    }
                    check(bad == 0, at + "the card is the sheet's pixels exactly (" + std::to_string(bad) + " differ)");
                }
            }
        }
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
}

// A copy of the shipped art in a new folder, in which the sheets of the scales in `cut` are cut a few bytes after their header: the header is a good one, the picture cannot be decoded
static std::vector<std::string> g_cut_folders;                                          // (removed at the end of test_unusable)
static std::string cut_art_folder(const std::string& tag, const std::vector<int32_t>& cut) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ants_hud_art_" + tag + "_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    fs::copy_file(art_folder() + "/hud_art.txt", dir / "hud_art.txt", fs::copy_options::overwrite_existing, ignored);
    for (int32_t scale = 1; scale <= 3; ++scale) {
        const std::string name = "sheet_" + std::to_string(scale) + "x.png";
        if (std::find(cut.begin(), cut.end(), scale) == cut.end()) {
            fs::copy_file(art_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
            continue;
        }
        std::ifstream in(art_folder() + "/" + name, std::ios::binary);
        std::vector<char> head(80);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(head.data(), in.gcount());
    }
    g_cut_folders.push_back(dir.string());
    return dir.string();
}

void test_unusable() {
    group("unusable", "a sheet that cannot be decoded, or that the device cannot make a texture, is said by preload_art and draws nothing; the same art is used again when a whole set is given");
    hud_art::Art art;
    assets::AssetArchive arc;
    if (!load_art(art) || !arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        check(false, "the art and the archive are read");
        return;
    }
    const QuietStdout quiet;
    ensure_sdl();
    SDL_Window* win = SDL_CreateWindow("hud-art-unusable", 0, 0, 960, 540, SDL_WINDOW_HIDDEN);
    Renderer renderer;
    if (win == nullptr || !renderer.init(win, arc)) {
        check(false, "the renderer is up");
        return;
    }
    renderer.set_canvas_size(960, 540);
    const auto drawn = [&](const hud_art::Art& what) {                          // (how many sheets are textures after one piece is asked for)
        renderer.begin_frame();
        renderer.draw_art(what, what.card, 0, 0, 182, 116, 0, 0, 182, 116);
        return renderer.art_textures();
    };

    // a sheet cut after its header passes the manifest (the header is right), and cannot be decoded when it is needed
    hud_art::Art cut;
    check(cut.load(cut_art_folder("cut1", {1})) && cut.loaded(), "a sheet with a good header and no body is read with the manifest");
    check(!renderer.preload_art(cut) && renderer.art_textures() == 0, "...preload_art says that its sheet cannot be made, and no texture is made");
    check(drawn(cut) == 0, "...a piece of it draws nothing, and nothing is thrown");
    check(!renderer.preload_art(cut), "...asked again it says the same");

    // only the sheet that the output needs counts: the cut one is the 1x sheet, and a window of 2x does not read it
    hud_art::Art cut_other;
    check(cut_other.load(cut_art_folder("cut3", {3})), "a folder with only its 3x sheet cut is read");
    check(renderer.art_scale_now() == 1 && renderer.preload_art(cut_other) && drawn(cut_other) == 1, "...and a window of 1x, which draws from the 1x sheet, makes its texture");

    // a whole set after a damaged one: another set is another generation, and is tried
    check(art.load(art_folder()) && renderer.preload_art(art) && drawn(art) == 1, "the whole art after a damaged set is used");

    // a device that takes no texture as large as the sheet
    renderer.set_art_texture_limit(500);
    hud_art::Art small_device;
    check(small_device.load(art_folder()) && !renderer.preload_art(small_device) && drawn(small_device) == 0, "a device whose largest texture is smaller than the sheet: preload_art says so, and nothing is drawn");
    renderer.set_art_texture_limit(0);
    check(small_device.load(art_folder()) && renderer.preload_art(small_device) && drawn(small_device) == 1, "(a device without a limit takes it)");

    renderer.shutdown();
    SDL_DestroyWindow(win);
    std::error_code ignored;
    for (const std::string& folder : g_cut_folders) std::filesystem::remove_all(folder, ignored);
    g_cut_folders.clear();
}
