// The HUD art, the model's part, continued (see test_hud_art.cpp): the manifest, the frame at every shape, the nine-slice and the choice of the sheet.
#include <set>
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
    refused(changed("sheetsize", "sheetsize 1000 800\nderive2x 0"), "a derive2x of no rows");
    refused(changed("sheetsize", "sheetsize 1000 800\nderive2x -4"), "a derive2x of a negative number of rows");
    refused(changed("sheetsize", "sheetsize 1000 800\nderive2x x"), "a derive2x that is not a number");
    refused(changed("sheetsize", "sheetsize 1000 800\nderive2x 801"), "a derive2x of more rows than the sheet has");
    refused(changed("sheetsize", "sheetsize 1000 800\nderive2x 40 40"), "a derive2x with two numbers");
    {
        Art art;
        check(art.parse(changed("sheetsize", "sheetsize 1000 800\nderive2x 800")) && art.derived_rows() == 800, "a derive2x of all the rows of the sheet is read");
        check(art.parse(changed("sheetsize", "sheetsize 1000 800\nderive2x 40")) && art.derived_rows() == 40, "a derive2x names the rows");
        check(art.parse(joined(small_manifest())) && art.derived_rows() == 0, "a manifest without one derives nothing");
    }
    {   // a second sheet, when the first is full: its size, its three files, and the sprites on it (`sprite2`)
        std::vector<std::string> lines = small_manifest();
        lines.insert(lines.begin() + 6, {"sheet2size 500 400", "sheet2 1 d.png", "sheet2 2 e.png", "sheet2 3 f.png"});
        lines.insert(lines.begin() + 11, "sprite2 extra 20 20 480 380 100 100 left top");
        Art art;
        check(art.parse(joined(lines)) && art.has_second_sheet() && art.sheet_width(1) == 500 && art.sheet_height(1) == 400 && art.sheet_width() == 1000 && art.sheet_file(2, 1) == "e.png" && art.sheet_file(2) == "b.png",
              "a second sheet is read: its size, its files, and the first sheet's are the first sheet's");
        const hud_art::Sprite* extra = art.sprite_at(art.find_sprite("extra"));
        const hud_art::Sprite* thing = art.sprite_at(art.find_sprite("thing"));
        check(extra != nullptr && extra->sheet == 1 && extra->sheet_x == 480 && thing != nullptr && thing->sheet == 0, "...a sprite2 is on the second sheet, a sprite on the first");
        Art plain;
        check(plain.parse(joined(small_manifest())) && !plain.has_second_sheet() && plain.sprite_at(plain.find_sprite("thing"))->sheet == 0 && plain.sheet_file(2, 1).empty(), "a manifest without one has none");
        const auto with = [&](const std::string& prefix, const std::string& line) {
            std::vector<std::string> l = lines;
            for (std::string& x : l) {
                if (x.compare(0, prefix.size(), prefix) == 0) {
                    x = line;
                    break;
                }
            }
            l.erase(std::remove(l.begin(), l.end(), std::string()), l.end());
            return joined(l);
        };
        refused(with("sprite2 extra", "sprite2 extra 20 20 490 380 100 100 left top"), "a sprite that ends outside the second sheet (but would be inside the first)");
        refused(with("sprite2 extra", "sprite2 extra 20 20 480 395 100 100 left top"), "a sprite that ends below the second sheet");
        refused(with("sheet2size", ""), "a second sheet without a size");
        refused(with("sheet2 3", ""), "a second sheet without its 3x file");
        refused(with("sheet2size", "sheet2size 0 400"), "a second sheet without a width");
        refused(with("sheet2 1", "sheet2 1 ../d.png"), "a second sheet outside the folder");
        refused(with("sheet2 1", "sheet2 4 d.png"), "a second sheet of the scale 4");
        refused(changed("sheet 1", "sheet 1 a.png\nsprite2 extra 20 20 480 380 100 100 left top"), "a sprite on a second sheet that the manifest does not have");
        refused(with("sprite thing", "sprite extra 10 10 200 0 800 100 right bottom"), "a sprite and a sprite2 of one name");
    }
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
    refused(changed("anim one", "anim one pointer"), "an animation marked with a word that is not cursor");
    refused(changed("anim one", "anim one cursor extra"), "an animation record with a fourth word");
    {   // a cursor animation: marked, and its parts keep the offsets from the click point (negative numbers are places like any other)
        std::vector<std::string> lines = small_manifest();
        for (std::string& l : lines) {
            if (l == "anim one") l = "anim one cursor";
            if (l == "frame 100 thing 800 100 card 778 136") l = "frame 100 thing -12 5";
        }
        Art art;
        const hud_art::Animation* one = art.parse(joined(lines)) ? art.find_animation("one") : nullptr;
        check(one != nullptr && one->cursor && one->frames.size() == 1 && one->frames[0].parts.size() == 1 && one->frames[0].parts[0].x == -12 && one->frames[0].parts[0].y == 5, "an animation marked cursor is read, with a negative offset");
        if (one != nullptr) {
            const std::vector<Draw> at = hud_art::cursor_draws(art, *one, 0, 300, 200);
            check(at.size() == 1 && at[0].dst.x == 288 && at[0].dst.y == 205 && at[0].dst.w == 10 && at[0].dst.h == 10 && at[0].src.w == 10, "its piece is drawn at the click point plus its offset, whatever the sprite's anchor is");
            check(hud_art::cursor_draws(art, *one, 1, 300, 200).empty(), "a frame that is not there draws nothing");
        }
        Art plain;
        check(plain.parse(joined(small_manifest())) && plain.find_animation("one") != nullptr && !plain.find_animation("one")->cursor, "an animation that is not marked is not a cursor");
    }
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
    // the set's own animations: the pieces of its windows that the original draws in another way (a switch held down, the window's fixed pieces, the countdown card's); every other is one of the original's
    const std::set<std::string> own = {"op_conh", "op_coffh", "op_honh", "op_hoffh", "op_window", "qh_window", "cd_card", "cd_row"};
    for (const hud_art::Animation& a : art.animations()) {
        check(own.count(a.name) != 0 || (arc_ptr != nullptr && arc.find_animation(a.name) != nullptr), "the art's animation " + a.name + " is one of the original's (a name that the game knows) or one of the windows' own");
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
            if (a.cursor) continue;                                                      // (a cursor's pieces stand at the click point, which can be anywhere: the cursors' own cases are in test_hud_art_cursors.cpp)
            if (a.name == "cd_row") continue;                                            // (its origin is the top left of a row's ground, wherever the game puts the row: its well starts a few pixels before it; see [windows])
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
        const Zone zones[] = {{"buthlpu", {layout.right(476 - 8), 7, 46, 23}}, {"butoptu", {layout.right(525 - 8), 7, 52, 23}}, {"butqitu", {layout.right(579 - 8), 7, 46, 23}},       // (8 pixels left of the original's: room for the Fullscreen button)
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

// A copy of the shipped art with a second sheet: the shipped sheets again, as sheet2_1x.png .. sheet2_3x.png (the second sheet of the manifest is as large as the first), and a sprite `second_card` on it that is the card; the
// second sheet's files are cut a few bytes after their header when `cut_second` is 1
static std::string second_sheet_folder(const std::string& tag, int cut_second) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ants_hud_art_" + tag + "_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    std::ifstream in(art_folder() + "/hud_art.txt", std::ios::binary);
    std::string manifest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string size_line, card_line;
    {
        std::istringstream lines(manifest);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("sheetsize ", 0) == 0) size_line = line;
            if (line.rfind("sprite card ", 0) == 0) card_line = line;
        }
    }
    const std::string extra = "sheet2size" + size_line.substr(std::string("sheetsize").size()) + "\nsheet2 1 sheet2_1x.png\nsheet2 2 sheet2_2x.png\nsheet2 3 sheet2_3x.png\nsprite2 second_card" + card_line.substr(std::string("sprite card").size()) + "\n";
    const size_t at = manifest.find("sprite ");
    manifest.insert(at, extra);
    std::ofstream out(dir / "hud_art.txt", std::ios::binary | std::ios::trunc);
    out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
    out.close();
    for (int scale = 1; scale <= 3; ++scale) {
        const std::string name = "sheet_" + std::to_string(scale) + "x.png";
        fs::copy_file(art_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
        const std::string second = "sheet2_" + std::to_string(scale) + "x.png";
        if (cut_second == 0) {
            fs::copy_file(art_folder() + "/" + name, dir / second, fs::copy_options::overwrite_existing, ignored);
            continue;
        }
        std::ifstream sheet(art_folder() + "/" + name, std::ios::binary);
        std::vector<char> head(80);
        sheet.read(head.data(), static_cast<std::streamsize>(head.size()));
        std::ofstream cut(dir / second, std::ios::binary | std::ios::trunc);
        cut.write(head.data(), sheet.gcount());
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

    // a second sheet (the first was full): a copy of the shipped sheets as sheet2_Nx.png with a sprite2 that is the card, and the same card on the first sheet
    {
        const std::string good = second_sheet_folder("second", 0);
        hud_art::Art two;
        check(two.load(good) && two.has_second_sheet() && two.find_sprite("second_card") >= 0 && two.sprite_at(two.find_sprite("second_card"))->sheet == 1, "a set with a second sheet is read: the header of each of its sheets is checked");
        DecodedImage first, second;
        check(two.sheet_pixels(1, first, 0) && two.sheet_pixels(1, second, 1) && first.rgba == second.rgba && second.width == two.sheet_width(1), "...the pixels of the second sheet are read from its own file");
        check(renderer.art_scale_now() == 1 && renderer.preload_art(two) && renderer.art_textures() == 2, "...preload_art makes the texture of each sheet at the output's scale");
        renderer.begin_frame();
        renderer.draw_art(two, two.find_sprite("second_card"), 0, 0, 182, 116, 0, 0, 182, 116);
        check(renderer.art_textures() == 2, "...a piece of the second sheet is drawn from its texture");
        // a second sheet that is cut after its header: the manifest passes, the frame is the first sheet's, and a piece of the second draws nothing
        hud_art::Art cut_second;
        check(cut_second.load(second_sheet_folder("second_cut", 1)) && cut_second.loaded(), "a second sheet cut after its header is read with the manifest");
        check(!renderer.preload_art(cut_second), "...preload_art says that the set cannot be drawn whole");
        renderer.begin_frame();
        renderer.draw_art(cut_second, cut_second.card, 0, 0, 182, 116, 0, 0, 182, 116);
        check(renderer.art_textures() == 1, "...the first sheet draws, the second is no texture");
        renderer.draw_art(cut_second, cut_second.find_sprite("second_card"), 0, 0, 182, 116, 0, 0, 182, 116);
        check(renderer.art_textures() == 1, "...and a piece of the second sheet draws nothing");
        // a device that takes a texture as large as the first sheet and not as large as the second (they are as large as each other here: the limit is the 1x sheet)
        renderer.set_art_texture_limit(500);
        hud_art::Art small_device;
        check(small_device.load(good) && !renderer.preload_art(small_device), "a device whose largest texture is smaller than the sheets says so for a set with two");
        renderer.set_art_texture_limit(0);
    }

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

// ---- the windows' pieces (docs/VIEW_AND_HUD.md, "Windows") --------------------------------------------------------------------------------------------------------------------

namespace {

std::string shipped_manifest() {
    std::ifstream f(art_folder() + "/hud_art.txt");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// The manifest without the first record whose line starts with `prefix`
std::string without_record(const std::string& text, const std::string& prefix) {
    const size_t at = text.find("\n" + prefix);
    if (at == std::string::npos) return text;
    const size_t end = text.find('\n', at + 1);
    return text.substr(0, at) + text.substr(end == std::string::npos ? text.size() : end);
}

/// The manifest without an animation and its frames
std::string without_anim(const std::string& text, const std::string& name) {
    const size_t at = text.find("\nanim " + name + "\n");
    if (at == std::string::npos) return text;
    size_t end = at + 1;
    for (;;) {
        end = text.find('\n', end);
        if (end == std::string::npos) return text.substr(0, at);
        end += 1;
        if (text.compare(end, 6, "frame ") != 0) break;
    }
    return text.substr(0, at) + "\n" + text.substr(end);
}

std::string replaced(const std::string& text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    return at == std::string::npos ? text : text.substr(0, at) + to + text.substr(at + from.size());
}

window_art::Pieces pieces_of(const std::string& manifest, const assets::AssetArchive& arc) {
    Art art;
    if (!art.parse(manifest)) return window_art::Pieces{};
    return window_art::check(art, arc);
}

/// How many times every pixel of `rect` is covered by the copies of one sprite (all of them must be one): returns false when a pixel is covered twice or not at all, or a copy leaves the rectangle
bool covers_once(const std::vector<Draw>& draws, int32_t sprite, const LayoutRect& rect) {
    std::vector<int> grid(static_cast<size_t>(rect.w) * static_cast<size_t>(rect.h), 0);
    for (const Draw& d : draws) {
        if (d.sprite != sprite) continue;
        if (d.dst.x < rect.x || d.dst.y < rect.y || d.dst.right() > rect.right() || d.dst.bottom() > rect.bottom() || d.dst.w != d.src.w || d.dst.h != d.src.h) return false;
        for (int32_t y = d.dst.y; y < d.dst.bottom(); ++y) {
            for (int32_t x = d.dst.x; x < d.dst.right(); ++x) ++grid[static_cast<size_t>(y - rect.y) * static_cast<size_t>(rect.w) + static_cast<size_t>(x - rect.x)];
        }
    }
    return std::all_of(grid.begin(), grid.end(), [](int n) { return n == 1; });
}

}  // namespace

void test_panel(const Art& art) {
    group("panel", "the nine pieces make a panel of any size: the clay tile over the whole rectangle once, the frame's pieces along its four sides and in its corners");
    check(hud_art::has_panel(art), "the shipped art has the nine pieces");
    const int32_t centre = art.find_sprite("panel_c");
    const hud_art::Sprite* corner = art.sprite_at(art.find_sprite("panel_tl"));
    check(corner != nullptr && corner->w == corner->h && corner->w >= 16, "the corner is a square of at least the frame's width");
    const int32_t c = corner != nullptr ? corner->w : 24;
    const LayoutRect sizes[] = {{100, 100, 320, 224}, {18, 20, 442, 440}, {0, 0, 640, 480}, {197, 98, 400, 346}, {5, 7, 330, 230}, {0, 0, 2 * c, 2 * c}, {3, 4, 2 * c + 1, 2 * c + 161}};
    for (const LayoutRect& r : sizes) {
        const std::string at = std::to_string(r.w) + " x " + std::to_string(r.h) + ": ";
        const std::vector<Draw> d = hud_art::panel_draws(art, r);
        check(!d.empty() && covers_once(d, centre, r), at + "the clay tile covers the rectangle exactly once, the last copies cut");
        // the frame: every piece stays inside the rectangle, the corners stand in the corners, and each edge is covered along its whole side between the corners
        bool inside = true;
        for (const Draw& x : d) inside = inside && x.dst.x >= r.x && x.dst.y >= r.y && x.dst.right() <= r.right() && x.dst.bottom() <= r.bottom();
        check(inside, at + "no piece leaves the rectangle");
        const auto copies = [&](const char* name) { std::vector<Draw> out; for (const Draw& x : d) if (x.sprite == art.find_sprite(name)) out.push_back(x); return out; };
        check(copies("panel_tl").size() == 1 && copies("panel_tl")[0].dst.x == r.x && copies("panel_tl")[0].dst.y == r.y, at + "the upper left corner");
        check(copies("panel_tr").size() == 1 && copies("panel_tr")[0].dst.right() == r.right() && copies("panel_tr")[0].dst.y == r.y, at + "the upper right corner");
        check(copies("panel_bl").size() == 1 && copies("panel_bl")[0].dst.x == r.x && copies("panel_bl")[0].dst.bottom() == r.bottom(), at + "the lower left corner");
        check(copies("panel_br").size() == 1 && copies("panel_br")[0].dst.right() == r.right() && copies("panel_br")[0].dst.bottom() == r.bottom(), at + "the lower right corner");
        int32_t top = 0, bottom = 0, left = 0, right = 0;
        for (const Draw& x : copies("panel_t")) top += x.dst.w;
        for (const Draw& x : copies("panel_b")) bottom += x.dst.w;
        for (const Draw& x : copies("panel_l")) left += x.dst.h;
        for (const Draw& x : copies("panel_r")) right += x.dst.h;
        check(top == r.w - 2 * c && bottom == r.w - 2 * c && left == r.h - 2 * c && right == r.h - 2 * c, at + "each edge is drawn along its whole side between the corners, no more");
    }
    check(hud_art::panel_draws(art, LayoutRect{0, 0, 2 * c - 1, 2 * c}).empty() && hud_art::panel_draws(art, LayoutRect{0, 0, 2 * c, 2 * c - 1}).empty(), "a rectangle that cannot hold the two corners is no panel");
    Art bare;
    check(bare.parse(without_record(shipped_manifest(), "sprite panel_br ")) && !hud_art::has_panel(bare) && hud_art::panel_draws(bare, sizes[0]).empty(), "a set that lacks a piece of the panel draws none");
}

void test_window_pieces(const assets::AssetArchive& arc, const Art& art) {
    group("windows", "which windows a set draws: every piece there and each at the original's place, else the window is the original's");
    const std::string text = shipped_manifest();
    const window_art::Pieces all = window_art::check(art, arc);
    check(all.dialogs && all.options && all.quick_help && all.countdown && all.fullscreen, "the shipped art has every window");
    // the countdown card's pieces that the game draws against: a row's well is placed from the top left of the row's ground (268 x 21) and holds it with a bezel round it, a color square is 11 x 11, the Fullscreen button
    // is a face of 94 x 22 and its cast shadow (3 across, 4 down), and the card's own pieces stand inside the card of 480 x 236
    const std::vector<Draw> row = hud_art::animation_draws_at(art, "cd_row", 0, 0, 0);
    check(row.size() == 1 && row[0].dst.x >= -6 && row[0].dst.x <= -4 && row[0].dst.y >= -6 && row[0].dst.y <= -4 && row[0].dst.right() >= 268 + 4 && row[0].dst.bottom() >= 21 + 4,
          "a countdown row is one well that starts 4 to 6 pixels left of and above the origin and reaches 4 or more beyond the row's 268 x 21");
    const auto size_is = [&](const char* name, int32_t w, int32_t h) { const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name)); return s != nullptr && s->w == w && s->h == h; };
    check(size_is("cd_dot_0", 11, 11) && size_is("cd_dot_1", 11, 11) && size_is("cd_dot_2", 11, 11) && size_is("cd_dot_3", 11, 11), "the four color squares of the countdown card are 11 x 11");
    check(size_is("dlg_btn_fullscreen_up", 97, 26) && size_is("dlg_btn_fullscreen_down", 97, 26), "the countdown card's Fullscreen button is a face of 94 x 22 and its cast shadow: 97 x 26");
    bool card_inside = true;
    for (const Draw& d : hud_art::animation_draws_at(art, "cd_card", 0, 0, 0)) card_inside = card_inside && d.dst.x >= 0 && d.dst.y >= 0 && d.dst.right() <= 480 && d.dst.bottom() <= 236;
    check(card_inside && !hud_art::animation_draws_at(art, "cd_card", 0, 0, 0).empty(), "the countdown card's fixed pieces are inside its 480 x 236");
    // a missing animation or sprite takes away its window and no other
    const struct { const char* what; std::string manifest; bool window_art::Pieces::*gone; } damaged[] = {
        {"the hover picture of Yes", without_anim(text, "yes2"), &window_art::Pieces::dialogs},
        {"the Accept button's pressed picture", without_anim(text, "dad_bacc3"), &window_art::Pieces::dialogs},
        {"the held picture of a switch", without_anim(text, "op_conh"), &window_art::Pieces::options},
        {"the options window's fixed pieces", without_anim(text, "op_window"), &window_art::Pieces::options},
        {"the pressed picture of Return to Game (the options window's)", without_anim(text, "breturn3"), &window_art::Pieces::options},
        {"the quick help's pieces", without_anim(text, "qh_window"), &window_art::Pieces::quick_help},
        {"the quick help's Return button", without_anim(text, "qh_return2"), &window_art::Pieces::quick_help},
        {"the countdown card's pieces", without_anim(text, "cd_card"), &window_art::Pieces::countdown},
        {"the countdown card's row", without_anim(text, "cd_row"), &window_art::Pieces::countdown},
        {"the countdown card's color square for the third color", without_record(text, "sprite cd_dot_2 "), &window_art::Pieces::countdown},
        {"the countdown card's pressed Fullscreen button", without_record(text, "sprite dlg_btn_fullscreen_down "), &window_art::Pieces::countdown},
        {"a picture of the Fullscreen button", without_record(text, "sprite button_fullscreen_leave_down "), &window_art::Pieces::fullscreen},
    };
    for (const auto& d : damaged) {
        const window_art::Pieces p = pieces_of(d.manifest, arc);
        check(!(p.*d.gone), std::string("without ") + d.what + ": that window is the original's");
        int others = 0;
        for (const bool b : {p.dialogs, p.options, p.quick_help, p.countdown, p.fullscreen}) others += b ? 1 : 0;
        check(others == 4, std::string("without ") + d.what + ": the other four windows stay");
    }
    check(!pieces_of(without_record(text, "sprite panel_c "), arc).dialogs && !pieces_of(without_record(text, "sprite panel_c "), arc).options, "without the panel no window has the new look");
    // a piece moved: a pixel is within the original's place, more than that is not
    const std::string yes = "frame 1000 dlg_btn_yes_up 180 260";
    check(text.find(yes) != std::string::npos, "(the manifest places the Yes button at the original's place)");
    check(pieces_of(replaced(text, yes, "frame 1000 dlg_btn_yes_up 181 261"), arc).dialogs, "a dialog button one pixel off the original's place still fits");
    check(!pieces_of(replaced(text, yes, "frame 1000 dlg_btn_yes_up 183 260"), arc).dialogs, "...three pixels right of it does not: the window is the original's");
    check(!pieces_of(replaced(text, "frame 1000 dlg_btn_return_up 351 425", "frame 1000 dlg_btn_return_up 351 421"), arc).options, "the options window's Return button four pixels up does not fit");
    // the rule itself
    const LayoutRect orig{10, 20, 46, 20};
    check(window_art::fits(LayoutRect{10, 20, 49, 24}, orig) && window_art::fits(LayoutRect{9, 19, 47, 21}, orig) && window_art::fits(LayoutRect{10, 20, 46, 20}, orig), "fits: the same place, with the cast shadow, or a pixel wider each way");
    check(!window_art::fits(LayoutRect{12, 20, 44, 20}, orig) && !window_art::fits(LayoutRect{10, 20, 51, 20}, orig) && !window_art::fits(LayoutRect{10, 17, 46, 23}, orig) && !window_art::fits(LayoutRect{10, 20, 46, 30}, orig), "fits: moved, too wide, too high, too tall: no");
}

void test_window_model(const assets::AssetArchive& arc) {
    group("window-model", "the veil covers the picture but the card, the pictures cut from the original's quick help, the Classic corner");
    // the veil and the card cover the picture once
    const LayoutRect pictures[] = {{0, 0, 640, 480}, {0, 0, 960, 540}, {0, 0, 1200, 540}};
    for (const LayoutRect& pic : pictures) {
        const LayoutRect card = window_art::kOptionsRect.moved(pic.w == 640 ? 0 : 160, pic.w == 640 ? 0 : 30);
        std::vector<int> grid(static_cast<size_t>(pic.w) * static_cast<size_t>(pic.h), 0);
        for (const LayoutRect& b : window_art::veil_bands(pic, card)) {
            check(b.w >= 0 && b.h >= 0, "a band has no negative size");
            for (int32_t y = b.y; y < b.bottom(); ++y) for (int32_t x = b.x; x < b.right(); ++x) ++grid[static_cast<size_t>(y) * static_cast<size_t>(pic.w) + static_cast<size_t>(x)];
        }
        bool once = true, card_clear = true;
        for (int32_t y = 0; y < pic.h; ++y) {
            for (int32_t x = 0; x < pic.w; ++x) {
                const int n = grid[static_cast<size_t>(y) * static_cast<size_t>(pic.w) + static_cast<size_t>(x)];
                if (card.contains(x, y)) card_clear = card_clear && n == 0; else once = once && n == 1;
            }
        }
        check(once && card_clear, std::to_string(pic.w) + " x " + std::to_string(pic.h) + ": the veil covers every pixel outside the card once and none inside it");
    }
    check(window_art::kVeil.r == 0 && window_art::kVeil.g == 0 && window_art::kVeil.b == 0 && window_art::kVeil.a > 100 && window_art::kVeil.a < 160, "the veil is an even dark: black at about a half");

    // the pictures of the quick help
    window_art::KeptPictures kept;
    check(kept.build(arc) && kept.ready(), "the original's two bitmaps give the quick help's pictures");
    struct Want { const char* name; int32_t x, y, w, h, opaque; };
    const Want want[] = {
        {"ant_win", 154, 52, 23, 42, 556},   {"ant_move", 148, 152, 26, 46, 626},  {"ant_food", 70, 304, 24, 46, 609},   {"pu_swim", 283, 137, 29, 28, 498},   {"pu_bomb", 283, 184, 32, 36, 556},
        {"pu_thief", 285, 234, 28, 26, 301}, {"pu_fire", 283, 284, 30, 21, 436},   {"pu_comb", 280, 334, 38, 26, 699},   {"ant_swim", 348, 132, 37, 46, 786},   {"ant_bomb", 352, 182, 31, 41, 597},
        {"ant_thief", 348, 227, 36, 41, 607}, {"ant_fire", 353, 271, 31, 48, 826}, {"ant_comb", 348, 322, 45, 49, 1142}, {"bomb_0", 560, 197, 12, 23, 187},    {"bomb_1", 580, 197, 12, 23, 187},
        {"bomb_2", 600, 197, 12, 23, 187},   {"fire", 583, 288, 31, 29, 617},      {"ant_steps", 276, 394, 38, 41, 659}, {"hill_0", 74, 409, 43, 43, 1849},     {"hill_1", 121, 409, 43, 43, 1849},
        {"hill_2", 168, 409, 43, 43, 1849},  {"hill_3", 215, 409, 43, 43, 1849},
    };
    check(kept.pictures().size() == sizeof(want) / sizeof(want[0]), "twenty-two pictures: eight ants, five power-ups, the bombs, the fire, the food's ant and the four hills");
    for (const window_art::KeptPictures::Picture& p : kept.pictures()) {
        const Want* w = nullptr;
        for (const Want& c : want) if (std::string(c.name) == p.name) w = &c;
        check(w != nullptr && p.x == w->x && p.y == w->y && p.w == w->w && p.h == w->h, std::string(p.name) + ": cut out at the place and size that the original's pictures have");
        int32_t opaque = 0, partly = 0;
        for (size_t i = 3; i < p.rgba.size(); i += 4) {
            opaque += p.rgba[i] == 255 ? 1 : 0;
            partly += p.rgba[i] != 0 && p.rgba[i] != 255 ? 1 : 0;
        }
        check(w != nullptr && opaque == w->opaque && partly == 0, std::string(p.name) + ": its own pixels are whole, the ground round it is see-through");
    }

    // the Classic corner
    window_art::ClassicCorner corner;
    check(corner.build(arc) && corner.ready() && corner.plate_w() == 171 && corner.plate().size() == 171u * 24u * 4u, "the corner's plate is 171 x 24");
    bool mirrored = true, opaque = true;
    for (int32_t y = 0; y < 24; ++y) {
        for (int32_t c = 0; c < 13; ++c) {
            for (int k = 0; k < 4; ++k) mirrored = mirrored && corner.plate()[(static_cast<size_t>(y) * 171 + static_cast<size_t>(c)) * 4 + static_cast<size_t>(k)] == corner.plate()[(static_cast<size_t>(y) * 171 + static_cast<size_t>(25 - c)) * 4 + static_cast<size_t>(k)];
        }
        for (int32_t x = 0; x < 171; ++x) opaque = opaque && corner.plate()[(static_cast<size_t>(y) * 171 + static_cast<size_t>(x)) * 4 + 3] == 255;
    }
    check(mirrored && opaque, "the plate is the plate's own end columns laid again, every second copy mirrored, no pixel see-through");
    for (const bool leave : {false, true}) {
        for (int state = 0; state < 3; ++state) check(corner.button(state, leave).size() == 20u * 23u * 4u, "the Classic button is 20 x 23");
        check(corner.button(0, leave) != corner.button(1, leave) && corner.button(0, leave) != corner.button(2, leave) && corner.button(1, leave) != corner.button(2, leave), "its three states differ");
    }
    check(corner.button(0, false) != corner.button(0, true), "the picture that leaves differs from the one that enters");
    // the button's left end is the Quit button's own first columns
    const int32_t quit = arc.find_sprite_id("butquitu.bmp");
    bool ends = quit >= 0;
    if (quit >= 0) {
        const assets::Sprite& s = arc.get_sprite(static_cast<uint32_t>(quit));
        for (uint32_t y = 0; y < 23; ++y) {
            for (uint32_t x : {0u, 1u, 2u}) {
                const assets::ColorRGBA& c = arc.get_palette()[s.get_pixel(x, y)];
                const uint8_t* got = &corner.button(0, false)[(static_cast<size_t>(y) * 20 + x) * 4];
                ends = ends && got[0] == c.r && got[1] == c.g && got[2] == c.b;
            }
        }
    }
    check(ends, "the button's left end is the original Quit button's first columns, pixel for pixel");
}
