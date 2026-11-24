// The world art (include/ants_app/world_art.hpp, src/ants_app/world_art.cpp, renderer_world_art.cpp; docs/WORLD_ART.md): the new pictures of the ground, its edges and the bridge, behind the setting that also
// chooses the HUD art (New, Classic).
//   [manifest]   the shipped manifest is read; every sprite of the ground's animations (431 .. 669) and of the bridge has a picture that fits the original sprite; the sheets are real RGBA pictures of the
//                sizes the manifest states and no side is above 4096 (a texture of a phone); a manifest that is wrong in any of the ways listed is refused; a variant that is not there is the plain picture
//   [variants]   which of its sixteen pictures a cell of plain ground shows: calm in the open, damp or stony beside another kind, worn and cracked in a corridor, stony round an object, the same for every game
//   [scale]      the world is drawn from the sheet of the output's scale, a zoomed pass from the 1x sheet, and a sheet the device cannot take gives the next smaller one
//   [draw]       the real renderer's ground at 1x, 2x and 3x is the sheet's picture of that cell, pixel for pixel, with the variant the rule chose
//   [classic]    without the art, or after it was taken away, the frame is the original's, byte for byte; the picture of a whole map (a thumbnail) is the original's whatever the setting is
//   [fallback]   a picture that does not fit, a sheet that is cut and a pack without a file are the original's sprites, never a hole
//   [fog]        the stipple that covers a cell Fog of War shrouds is the same pixels with either art, and the cells in the open have the new ground
//   [hash]       the state of a match is the same whether or not the new pictures are drawn
// Usage: test_world_art. Exit code 0 when every check passes. With ANTS_WORLD_ART_SHOTS=FOLDER it writes the pictures of the batch there instead of testing (below).
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/png_decode.hpp"
#include "ants_app/png_encode.hpp"
#include "ants_app/world_art.hpp"

using namespace ants;
using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

std::string pack_folder() { return std::string(ANTS_SOURCE_DIR) + "/assets/world_art"; }

bool read_text(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

/// A copy of the shipped pack in a folder of its own, with the manifest changed by `edit` and the sheets cut after their header where `cut` says (the first 80 bytes stay: the header is right, the body is not)
std::string pack_copy(const std::string& tag, const std::function<std::string(const std::string&)>& edit, const std::vector<int32_t>& cut = {}) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ants_world_art_" + tag + "_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    std::string text;
    read_text(pack_folder() + "/world_art.txt", text);
    {
        std::ofstream out(dir / "world_art.txt", std::ios::binary | std::ios::trunc);
        out << edit(text);
    }
    for (int32_t scale = 1; scale <= 3; ++scale) {
        const std::string name = "sheet_" + std::to_string(scale) + "x.png";
        if (std::find(cut.begin(), cut.end(), scale) == cut.end()) {
            fs::copy_file(pack_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
            continue;
        }
        std::ifstream in(pack_folder() + "/" + name, std::ios::binary);
        std::vector<char> head(80);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(head.data(), in.gcount());
    }
    return dir.string();
}

std::string identity(const std::string& s) { return s; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [manifest]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// A tiny manifest: the header, three sheet rows and the given sprite rows
std::string tiny(const std::string& sprites, const std::string& sheets = "sheet 1 a.png 100 100\nsheet 2 b.png 200 200\nsheet 3 c.png 300 300\n") {
    return "ants-world-art 1\n" + sheets + sprites;
}

void test_manifest(const assets::AssetArchive& arc) {
    group("manifest", "the shipped manifest and its sheets, and every way to break a manifest");
    world_art::Pack pack;
    check(pack.load(pack_folder()) && pack.loaded(), "the shipped pack is read: " + pack.what());
    check(pack.pictures().size() == 667, "the pack holds the 667 pictures of the batch (the plain ones and their variants): " + std::to_string(pack.pictures().size()));
    for (const int32_t scale : world_art::kScales) {
        check(pack.sheet_width(scale) > 0 && pack.sheet_width(scale) <= 4096 && pack.sheet_height(scale) > 0 && pack.sheet_height(scale) <= 4096, "the sheet of scale " + std::to_string(scale) + " fits a texture of 4096");
        DecodedImage image;
        check(pack.sheet_pixels(scale, image) && image.width == pack.sheet_width(scale) && image.height == pack.sheet_height(scale), "the sheet of scale " + std::to_string(scale) + " decodes to the size the manifest states");
    }

    // every sprite that the ground's animations and the bridge use has a picture that fits the original's sprite
    std::set<uint32_t> wanted;
    for (uint32_t id = 431; id <= 669; ++id) {
        for (const auto& sub : arc.get_animation(id).subitems) {
            for (const auto& f : sub.frames) wanted.insert(f.sprite_index);
        }
    }
    for (uint32_t s = 112; s <= 116; ++s) wanted.insert(s);
    size_t unfit = 0, lacking = 0;
    for (const uint32_t s : wanted) {
        const world_art::Picture* p = pack.find(s, 0);
        if (p == nullptr) {
            ++lacking;
            continue;
        }
        const auto& sp = arc.get_sprite(s);
        if (p->w != static_cast<int32_t>(sp.width) || p->h != static_cast<int32_t>(sp.height)) ++unfit;
    }
    check(wanted.size() == 292 && lacking == 0 && unfit == 0, "all " + std::to_string(wanted.size()) + " sprites of the ground and the bridge have a picture of the original sprite's size (lacking " + std::to_string(lacking) + ", unfit " + std::to_string(unfit) + ")");

    // a variant that the pack does not have is the plain picture; one that it has is that one
    check(pack.find(726, 5) != nullptr && pack.find(726, 5)->variant == 5, "the gravel tile has its sixth picture");
    check(pack.find(112, 9) != nullptr && pack.find(112, 9)->variant == 0, "a bridge sprite has no variants: the plain picture is given for any");
    check(pack.find(9999, 0) == nullptr && pack.find(0, 3) == nullptr, "a sprite that the pack has not is none");

    // ways to break it
    world_art::Pack p;
    const std::string good = tiny("sprite 1 0 10 10 1 1 1 1 1 1\nsprite 2 0 10 10 20 1 40 1 60 1\n");
    check(p.parse(good) && p.pictures().size() == 2, "a small manifest is read");
    check(!p.parse(""), "an empty manifest is refused");
    check(!p.parse("ants-world-art 2\n"), "another version is refused");
    check(!p.parse("ants-hud-art 1\n"), "another header is refused");
    check(!p.parse(good.substr(0, good.find('\n') + 1)), "a manifest without sheets is refused");
    check(!p.parse(tiny("", "sheet 1 a.png 100 100\nsheet 3 c.png 300 300\n")), "a missing sheet row is refused");
    check(!p.parse(tiny("", "sheet 2 b.png 200 200\nsheet 1 a.png 100 100\nsheet 3 c.png 300 300\n")), "sheet rows out of order are refused");
    check(!p.parse(tiny("", "sheet 1 a.png 100 100\nsheet 2 b.png 200 200\nsheet 3 c.png 300 300\nsheet 3 d.png 300 300\n")), "a sheet row twice is refused");
    check(!p.parse(tiny("", "sheet 1 ../a.png 100 100\nsheet 2 b.png 200 200\nsheet 3 c.png 300 300\n")), "a sheet in another folder is refused");
    check(!p.parse(tiny("", "sheet 1 sub/a.png 100 100\nsheet 2 b.png 200 200\nsheet 3 c.png 300 300\n")), "a sheet in a sub folder is refused");
    check(!p.parse(tiny("", "sheet 1 a.png 0 100\nsheet 2 b.png 200 200\nsheet 3 c.png 300 300\n")), "a sheet of no size is refused");
    check(!p.parse("sprite 1 0 10 10 1 1 1 1 1 1\n" + tiny("")), "a sprite before the header is refused");
    check(!p.parse("ants-world-art 1\nsprite 1 0 10 10 1 1 1 1 1 1\nsheet 1 a.png 100 100\nsheet 2 b.png 200 200\nsheet 3 c.png 300 300\n"), "a sprite before the sheets is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 1 1 1 1\n")), "a sprite row that is short is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 1 1 1 1 1 x\n")), "a sprite row that holds a word is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 1 1 1 1 1 1\nsprite 1 0 10 10 20 1 40 1 60 1\n")), "a picture twice is refused");
    check(!p.parse(tiny("sprite 2 0 10 10 1 1 1 1 1 1\nsprite 1 0 10 10 20 1 40 1 60 1\n")), "pictures out of order are refused");
    check(!p.parse(tiny("sprite 1 16 10 10 1 1 1 1 1 1\n")), "a variant above 15 is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 1 1 1 1 1 1\n")), "a picture of no width is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 0 1 1 1 1 1\n")), "a picture with no room for its ring (x = 0) is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 1 0 1 1 1 1\n")), "a picture with no room for its ring (y = 0) is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 90 1 1 1 1 1\n")), "a picture whose ring crosses the sheet's right side at 1x is refused");
    check(!p.parse(tiny("sprite 1 0 10 10 1 1 1 190 1 1\n")), "a picture whose ring crosses the sheet's bottom at 2x is refused");
    check(p.parse(tiny("sprite 1 0 10 10 89 89 179 179 269 269\n")), "a picture that ends exactly at its ring against the side is read");
    check(!p.parse(tiny("sprite 1 0 10 10 1 1 1 1 1 1\nblob 3\n")), "a record of another kind is refused");
    check(!p.parse("ants-world-art 1\r\nsheet 1 a.png 100 100\r\n"), "CR LF line ends are refused");
    check(!p.parse("not a manifest") && !p.loaded() && p.pictures().empty(), "a manifest that was refused leaves a pack that is not loaded and holds nothing");
    world_art::Pack gone;
    check(!gone.load(pack_folder() + "/nothing here") && !gone.loaded(), "a folder without a manifest is not a pack");
    const std::string cut = pack_copy("manifest_cut", identity, {2});
    world_art::Pack cut_pack;
    check(cut_pack.load(cut) && cut_pack.loaded(), "a sheet with a good header and no body passes the header look");
    DecodedImage image;
    check(!cut_pack.sheet_pixels(2, image) && image.width == 0, "...and cannot be decoded when it is needed");
    const std::string wrong = pack_copy("manifest_wrong", [](const std::string& t) {
        std::string s = t;
        const size_t at = s.find("sheet 3 sheet_3x.png 3072");                       // the 3x sheet is stated one pixel narrower than the file: the file is not the picture the manifest says
        if (at != std::string::npos) s.replace(at, 25, "sheet 3 sheet_3x.png 3071");
        return s;
    });
    world_art::Pack wrong_pack;
    check(!wrong_pack.load(wrong) && !wrong_pack.loaded(), "a sheet that is not the size the manifest states is refused at the start");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [variants]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

world_art::GroundMap make_map(int32_t w, int32_t h, char kind, bool plain) {
    world_art::GroundMap m;
    m.width = w;
    m.height = h;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    m.kind.assign(n, plain ? kind : 0);
    m.plain.assign(n, plain ? 1 : 0);
    m.object.assign(n, 0);
    return m;
}

size_t at_of(const world_art::GroundMap& m, int32_t x, int32_t y) { return static_cast<size_t>(y) * static_cast<size_t>(m.width) + static_cast<size_t>(x); }

bool in_range(uint8_t v, int lo, int hi) { return v >= lo && v <= hi; }

void test_variants() {
    group("variants", "which of its sixteen pictures a cell of plain ground shows");
    // the open plain: every picture is one of the sixteen, the calm ones are the most, and the same map gives the same pictures
    world_art::GroundMap open = make_map(40, 40, 'g', true);
    const std::vector<uint8_t> a = world_art::ground_variants(open);
    check(a.size() == 1600, "one picture for every cell");
    check(a == world_art::ground_variants(open), "the same map gives the same pictures");
    size_t calm = 0, stony = 0, damp = 0, worn = 0, cracked = 0, over = 0;
    for (const uint8_t v : a) {
        if (v >= world_art::kVariants) ++over;
        else if (v <= 5) ++calm;
        else if (v <= 8) ++stony;
        else if (v <= 11) ++damp;
        else if (v <= 13) ++worn;
        else ++cracked;
    }
    check(over == 0, "no cell shows a picture above the fifteenth");
    check(calm > stony && calm > damp && calm > worn && calm > cracked, "in the open plain the calm pictures are the most");
    check(calm >= 700 && stony > 0 && worn > 0 && cracked > 0, "...and the stony, worn and cracked ones are there too: calm " + std::to_string(calm) + ", stony " + std::to_string(stony) + ", worn " + std::to_string(worn) + ", cracked " + std::to_string(cracked));
    check(damp == 0, "...and no damp one: the map's border is not an edge");
    std::set<uint8_t> calm_pictures;
    for (const uint8_t v : a) {
        if (v <= 5) calm_pictures.insert(v);
    }
    check(calm_pictures.size() == 6, "all six calm pictures are used");

    // a cell that is not plain ground shows the first picture
    world_art::GroundMap mixed = make_map(12, 12, 'g', true);
    for (int32_t y = 4; y < 8; ++y) {
        for (int32_t x = 4; x < 8; ++x) {
            mixed.plain[at_of(mixed, x, y)] = 0;
            mixed.kind[at_of(mixed, x, y)] = 'g';
        }
    }
    const std::vector<uint8_t> b = world_art::ground_variants(mixed);
    bool not_plain_is_zero = true;
    for (int32_t y = 4; y < 8; ++y) {
        for (int32_t x = 4; x < 8; ++x) not_plain_is_zero = not_plain_is_zero && b[at_of(mixed, x, y)] == 0;
    }
    check(not_plain_is_zero, "an edge tile or an object's ground shows the plain picture");
    // beside it the ground is damp or stony (9 .. 11, 6 .. 8)
    bool beside = true;
    for (int32_t x = 3; x < 9; ++x) beside = beside && in_range(b[at_of(mixed, x, 3)], 6, 11) && in_range(b[at_of(mixed, x, 8)], 6, 11);
    for (int32_t y = 3; y < 9; ++y) beside = beside && in_range(b[at_of(mixed, 3, y)], 6, 11) && in_range(b[at_of(mixed, 8, y)], 6, 11);
    check(beside, "the plain ground next to a tile that is not the same kind is damp or stony");
    int32_t damp_beside = 0;
    for (int32_t x = 3; x < 9; ++x) damp_beside += in_range(b[at_of(mixed, x, 3)], 9, 11) ? 1 : 0;
    check(damp_beside >= 3, "...mostly damp: " + std::to_string(damp_beside) + " of the six above it");

    // another kind of ground is an edge as well: gravel beside dirt
    world_art::GroundMap two = make_map(12, 12, 'g', true);
    for (int32_t y = 0; y < 12; ++y) {
        for (int32_t x = 6; x < 12; ++x) two.kind[at_of(two, x, y)] = 'd';
    }
    const std::vector<uint8_t> c = world_art::ground_variants(two);
    check(in_range(c[at_of(two, 5, 6)], 6, 11) && in_range(c[at_of(two, 6, 6)], 6, 11), "gravel and dirt side by side are each other's edge");
    check(!in_range(c[at_of(two, 2, 6)], 9, 11) && !in_range(c[at_of(two, 10, 6)], 9, 11), "...and a cell three away from the edge is not damp");

    // a corridor three cells across: its middle row is worn or cracked, its outer rows are damp or stony
    world_art::GroundMap corridor = make_map(24, 9, 'g', false);
    for (int32_t y = 3; y < 6; ++y) {
        for (int32_t x = 0; x < 24; ++x) {
            corridor.plain[at_of(corridor, x, y)] = 1;
            corridor.kind[at_of(corridor, x, y)] = 'g';
        }
    }
    const std::vector<uint8_t> d = world_art::ground_variants(corridor);
    bool middle = true, outer = true;
    for (int32_t x = 0; x < 24; ++x) {
        middle = middle && in_range(d[at_of(corridor, x, 4)], 12, 15);
        outer = outer && in_range(d[at_of(corridor, x, 3)], 6, 11) && in_range(d[at_of(corridor, x, 5)], 6, 11);
    }
    check(middle, "the middle of a corridor is worn or cracked");
    check(outer, "...and its sides are damp or stony");

    // the cells round an object are scuffed (stony) about one in two, in the open plain
    world_art::GroundMap objects = make_map(40, 40, 'g', true);
    objects.object[at_of(objects, 20, 20)] = 1;
    const std::vector<uint8_t> plain_pictures = world_art::ground_variants(open);
    const std::vector<uint8_t> e = world_art::ground_variants(objects);
    int32_t scuffed = 0, changed = 0;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            scuffed += in_range(e[at_of(objects, 20 + dx, 20 + dy)], 6, 8) ? 1 : 0;
            changed += e[at_of(objects, 20 + dx, 20 + dy)] != plain_pictures[at_of(open, 20 + dx, 20 + dy)] ? 1 : 0;
        }
    }
    check(scuffed >= 3 && changed >= 1, "round an object the ground is scuffed: " + std::to_string(scuffed) + " of its nine cells are stony");
    bool far_same = true;
    for (int32_t y = 0; y < 40; ++y) {
        for (int32_t x = 0; x < 40; ++x) {
            if (std::abs(x - 20) > 2 || std::abs(y - 20) > 2) far_same = far_same && e[at_of(objects, x, y)] == plain_pictures[at_of(open, x, y)];
        }
    }
    check(far_same, "...and nothing changes further than the cell's neighbors");

    // a map the arrays of which do not match its size, or of no size, gives nothing to draw
    world_art::GroundMap broken = make_map(4, 4, 'g', true);
    broken.object.pop_back();
    check(world_art::ground_variants(broken) == std::vector<uint8_t>(16, 0), "a map whose arrays are not its size shows the plain picture everywhere");
    check(world_art::ground_variants(world_art::GroundMap{}).empty(), "a map of no size has no pictures");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The renderer's cases
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kW = 960;
constexpr int32_t kH = 540;

/// The pixels of a block of the picture
struct Block {
    int32_t x{0}, y{0}, w{0}, h{0};
};

bool same_pixels(const zoomtest::Picture& a, const zoomtest::Picture& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }

bool same_block(const zoomtest::Picture& a, const zoomtest::Picture& b, const Block& r) {
    for (int32_t y = r.y; y < r.y + r.h; ++y) {
        if (std::memcmp(a.at(r.x, y), b.at(r.x, y), static_cast<size_t>(r.w) * 4u) != 0) return false;
    }
    return true;
}

/// A cell of the view that shows one single-frame ground tile whose picture is opaque (a plain tile or an edge): its column and row, the sprite and the picture the pack has for it with the variant the renderer chose
struct Cell {
    int32_t col{-1};
    int32_t row{-1};
    uint32_t sprite{0};
    const world_art::Picture* picture{nullptr};
};

/// Whether the cell of a picture of the original is just its ground: the block of the frame is the original's sprite, enlarged by the output's scale (no plant, ant, stone or fog over it)
bool block_is_original(const assets::AssetArchive& arc, const zoomtest::Picture& frame, const Block& r, uint32_t sprite, int32_t os) {
    const std::vector<uint8_t> sp = arc.get_sprite(sprite).to_rgba32(arc.get_palette());
    for (int32_t y = 0; y < r.h; ++y) {
        for (int32_t x = 0; x < r.w; ++x) {
            if (std::memcmp(frame.at(r.x + x, r.y + y), &sp[(static_cast<size_t>(y / os) * 32u + static_cast<size_t>(x / os)) * 4u], 3) != 0) return false;
        }
    }
    return true;
}

Block cell_block(zoomtest::PixelRig& rig, int32_t col, int32_t row, int32_t os);

Cell find_cell(zoomtest::PixelRig& rig, const world_art::Pack& pack, const DecodedImage& sheet3, bool want_plain, const zoomtest::Picture& classic, int32_t os, int32_t skip = 0) {
    const ViewportCamera& cam = rig.cam();
    const int32_t c0 = static_cast<int32_t>(cam.x) / 32 + 1, r0 = static_cast<int32_t>(cam.y) / 32 + 1;
    for (int32_t r = r0; r < r0 + 12; ++r) {
        for (int32_t c = c0; c < c0 + 18; ++c) {
            const auto& cell = rig.engine.grid().get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            if (cell.terrain_id < 431 || cell.terrain_id > 669) continue;
            const auto& anim = rig.arc.get_animation(cell.terrain_id);
            if (anim.subitems.size() != 1 || anim.subitems[0].frames.size() != 1) continue;
            const bool plain = anim.name.size() >= 3 && anim.name[1] == '0' && anim.name[2] == '1';
            if (plain != want_plain) continue;
            const uint32_t sprite = anim.subitems[0].frames[0].sprite_index;
            const uint8_t variant = rig.renderer.ground_variant_at(c, r);
            const world_art::Picture* pic = pack.find(sprite, variant);
            if (pic == nullptr || pic->w != 32 || pic->h != 32) continue;
            bool opaque = true;                                                    // (the 3x picture says it for all scales)
            for (int32_t y = 0; y < 96 && opaque; ++y) {
                for (int32_t x = 0; x < 96; ++x) opaque = opaque && sheet3.rgba[(static_cast<size_t>(pic->y[2] + y) * static_cast<size_t>(sheet3.width) + static_cast<size_t>(pic->x[2] + x)) * 4u + 3u] == 255;
            }
            if (!opaque) continue;
            if (!block_is_original(rig.arc, classic, cell_block(rig, c, r, os), sprite, os)) continue;      // (something stands on it: not a cell to judge the ground by)
            if (skip-- > 0) continue;
            return Cell{c, r, sprite, pic};
        }
    }
    return Cell{};
}

/// Where a cell's top left is in the picture of an output of `os` pixels for each canvas pixel
Block cell_block(zoomtest::PixelRig& rig, int32_t col, int32_t row, int32_t os) {
    const LayoutRect view = rig.layout.view();
    const ViewportCamera& cam = rig.cam();
    const int32_t sx = view.x + col * 32 - static_cast<int32_t>(cam.x);
    const int32_t sy = view.y + row * 32 - static_cast<int32_t>(cam.y);
    return Block{sx * os, sy * os, 32 * os, 32 * os};
}

/// Whether the block of the frame is the picture of the pack at scale `scale`, pixel for pixel
bool block_is_picture(const zoomtest::Picture& frame, const Block& r, const world_art::Picture& pic, const DecodedImage& sheet, int32_t scale) {
    const size_t k = static_cast<size_t>(scale - 1);
    for (int32_t y = 0; y < r.h; ++y) {
        for (int32_t x = 0; x < r.w; ++x) {
            const uint8_t* got = frame.at(r.x + x, r.y + y);
            const uint8_t* want = &sheet.rgba[(static_cast<size_t>(pic.y[k] + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(pic.x[k] + x)) * 4u];
            if (std::memcmp(got, want, 3) != 0) return false;
        }
    }
    return true;
}

void look_at_treasure(zoomtest::PixelRig& rig) { rig.look(320, 672); }

void test_draw(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("draw", "the real renderer's ground at 1x, 2x and 3x is the sheet's picture of the cell, with the variant the rule chose");
    DecodedImage sheets[3];
    for (const int32_t s : world_art::kScales) {
        if (!pack.sheet_pixels(s, sheets[s - 1])) {
            check(false, "the sheet of scale " + std::to_string(s) + " is read");
            return;
        }
    }
    for (const int32_t os : {1, 2, 3}) {
        zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, os);
        if (!rig.ok) {
            check(false, "the rig is up");
            return;
        }
        look_at_treasure(rig);
        const zoomtest::Picture classic = rig.shoot();
        rig.renderer.set_world_art(&pack);
        check(rig.renderer.world_art_scale_now() == os, "the output of " + std::to_string(os) + " pixels for a canvas pixel draws from the sheet of scale " + std::to_string(os));
        const zoomtest::Picture art = rig.shoot();
        check(rig.renderer.world_art_textures() == 1, "...and only that sheet is a texture");
        check(art.w == kW * os && art.h == kH * os && !same_pixels(art, classic), "the frame at " + std::to_string(os) + "x is not the original's");
        size_t plain_checked = 0, edge_checked = 0;
        for (int32_t k = 0; k < 6; ++k) {
            const Cell cell = find_cell(rig, pack, sheets[2], true, classic, os, k * 3);
            if (cell.picture == nullptr) break;
            const Block r = cell_block(rig, cell.col, cell.row, os);
            check(block_is_picture(art, r, *cell.picture, sheets[os - 1], os), "the plain tile of the cell (" + std::to_string(cell.col) + ", " + std::to_string(cell.row) + ") at " + std::to_string(os) + "x is its picture (variant " + std::to_string(rig.renderer.ground_variant_at(cell.col, cell.row)) + ")");
            ++plain_checked;
        }
        for (int32_t k = 0; k < 4; ++k) {
            const Cell cell = find_cell(rig, pack, sheets[2], false, classic, os, k * 2);
            if (cell.picture == nullptr) break;
            const Block r = cell_block(rig, cell.col, cell.row, os);
            check(block_is_picture(art, r, *cell.picture, sheets[os - 1], os), "the edge tile of the cell (" + std::to_string(cell.col) + ", " + std::to_string(cell.row) + ") at " + std::to_string(os) + "x is its picture");
            ++edge_checked;
        }
        check(plain_checked >= 3, "at least three plain tiles were looked at: " + std::to_string(plain_checked));
        check(edge_checked >= 2, "at least two edge tiles were looked at: " + std::to_string(edge_checked));
    }

    // the choice of the picture is by the cell: two cells of the same plain tile show different pictures somewhere on the map
    zoomtest::PixelRig rig(arc, "TREASURE", kW, kH);
    rig.renderer.set_world_art(&pack);
    std::set<uint8_t> shown;
    for (int32_t r = 0; r < 20; ++r) {
        for (int32_t c = 0; c < 20; ++c) shown.insert(rig.renderer.ground_variant_at(c, r));
    }
    check(shown.size() >= 4, "the ground of one map shows several of the sixteen pictures: " + std::to_string(shown.size()));
    check(rig.renderer.ground_variant_at(-1, 3) == 0 && rig.renderer.ground_variant_at(3, 100000) == 0, "a cell outside the map shows the plain picture");

    // a bridge is drawn from the pack as well (its sprites are 112 .. 116)
    for (uint32_t s = 112; s <= 115; ++s) check(pack.find(s, 0) != nullptr, "the bridge's stage " + std::to_string(s - 111) + " has a picture");
}

void test_scale(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("scale", "the world pass's sheet by the output's scale; a zoomed pass uses the 1x sheet; a device without room for a sheet gets the next smaller one");
    {
        zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, 3);
        rig.renderer.set_world_art(&pack);
        look_at_treasure(rig);
        rig.look(320, 672, 1.5f);
        rig.shoot();
        check(rig.renderer.world_art_textures() == 1 && rig.renderer.world_art_scale_now() == 3, "a zoomed frame of an output of 3x makes the 1x sheet's texture only");
        rig.look(320, 672, 1.0f);
        rig.shoot();
        check(rig.renderer.world_art_textures() == 2, "...and the frame at the zoom 1 the 3x sheet's");
    }
    {
        zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, 3);
        const zoomtest::Picture classic = [&]() { look_at_treasure(rig); return rig.shoot(); }();
        rig.renderer.set_world_art(&pack);
        rig.renderer.set_art_texture_limit(2100);                                // the 3x sheet (3072 wide) is too wide, the 2x one (2048) is not
        const zoomtest::Picture two = rig.shoot();
        check(rig.renderer.world_art_textures() == 1 && !same_pixels(two, classic), "a device that takes no texture of 3072 gets the 2x sheet at the 3x output");
        rig.renderer.set_world_art(nullptr);
        rig.renderer.set_world_art(&pack);
        rig.renderer.set_art_texture_limit(1000);                                // not even the 1x sheet (1024)
        const zoomtest::Picture none = rig.shoot();
        check(rig.renderer.world_art_textures() == 0 && same_pixels(none, classic), "a device that takes no sheet at all draws the original's frame");
        rig.renderer.set_art_texture_limit(0);
        rig.renderer.set_world_art(nullptr);
        rig.renderer.set_world_art(&pack);
        const zoomtest::Picture again = rig.shoot();
        check(rig.renderer.world_art_textures() == 1 && !same_pixels(again, classic), "the same pack is used again when the device takes the sheets");
    }
}

void test_classic(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("classic", "without the art, or after it was taken away, the frame is the original's, byte for byte; a map's picture is the original's whatever the setting");
    for (const int32_t os : {1, 2}) {
        zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, os);
        look_at_treasure(rig);
        const zoomtest::Picture classic = rig.shoot();
        rig.renderer.set_world_art(&pack);
        const zoomtest::Picture art = rig.shoot();
        check(!same_pixels(art, classic), "the art changes the frame at " + std::to_string(os) + "x");
        rig.renderer.set_world_art(nullptr);
        check(rig.renderer.world_art() == nullptr && rig.renderer.world_art_textures() == 0, "taking the art away drops its textures");
        check(same_pixels(rig.shoot(), classic), "the frame at " + std::to_string(os) + "x after the art was taken away is the original's, byte for byte");
        rig.look(320, 672, 0.5f);
        const zoomtest::Picture small_classic = rig.shoot();
        rig.renderer.set_world_art(&pack);
        rig.look(320, 672, 0.5f);
        check(!same_pixels(rig.shoot(), small_classic), "a frame zoomed out has the art too");
    }
    // the picture of a whole map (the preview, the thumbnail) is the original's look
    zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH);
    WorldImage plain_image, art_image;
    std::string why;
    check(rig.renderer.render_world_image(rig.level, rig.world, rig.engine.grid(), plain_image, &why), "the picture of a map is made: " + why);
    rig.renderer.set_world_art(&pack);
    check(rig.renderer.render_world_image(rig.level, rig.world, rig.engine.grid(), art_image, &why), "...and with the art set: " + why);
    check(plain_image.width == art_image.width && plain_image.rgba == art_image.rgba, "the picture of a map is the same with the art set");
    check(rig.renderer.world_art() == &pack, "...and the art is still set after it");
}

void test_fallback(const assets::AssetArchive& arc, const world_art::Pack&) {
    group("fallback", "a picture that does not fit, a cut sheet and a pack without its file leave the original's sprites");
    zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, 2);
    look_at_treasure(rig);
    const zoomtest::Picture classic = rig.shoot();
    const zoomtest::QuietStdout quiet;

    // every picture states a size that is not the original's sprite's: none is used
    const std::string wide = pack_copy("wide", [](const std::string& t) {
        std::istringstream in(t);
        std::string line, out;
        while (std::getline(in, line)) {
            if (line.rfind("sprite ", 0) == 0) {
                std::istringstream words(line);
                std::string word, number, variant;
                int w = 0, h = 0;
                words >> word >> number >> variant >> w >> h;
                std::string rest;
                std::getline(words, rest);
                line = "sprite " + number + " " + variant + " " + std::to_string(w + 1) + " " + std::to_string(h) + rest;
            }
            out += line + "\n";
        }
        return out;
    });
    world_art::Pack wide_pack;
    check(wide_pack.load(wide) && wide_pack.loaded(), "a pack whose pictures are all one pixel too wide is read");
    rig.renderer.set_world_art(&wide_pack);
    check(same_pixels(rig.shoot(), classic), "...and draws the original's sprites, for none fits");

    // a sheet that is cut after its header: the manifest passes, the texture cannot be made; the output gets the next smaller sheet (the 1x one), and a pack with no sheet that can be had is the original's frame
    world_art::Pack cut_pack;
    check(cut_pack.load(pack_copy("cut2", identity, {2})), "a pack with a cut 2x sheet is read");
    rig.renderer.set_world_art(&cut_pack);
    const zoomtest::Picture smaller = rig.shoot();
    check(!same_pixels(smaller, classic) && rig.renderer.world_art_textures() == 1, "...and a 2x output draws from its 1x sheet: one texture, not the original's frame");
    check(same_pixels(rig.shoot(), smaller) && rig.renderer.world_art_textures() == 1, "...and again the same (a sheet that failed is not read at every frame)");
    world_art::Pack all_cut;
    check(all_cut.load(pack_copy("cut123", identity, {1, 2, 3})), "a pack with all three sheets cut is read");
    rig.renderer.set_world_art(&all_cut);
    check(same_pixels(rig.shoot(), classic) && rig.renderer.world_art_textures() == 0, "...and its frame is the original's, with no texture");
    check(same_pixels(rig.shoot(), classic), "...and again");

    // the cut sheet is not the one a 1x output needs: that output has its art
    zoomtest::PixelRig one(arc, "TREASURE", kW, kH, 0, 1);
    look_at_treasure(one);
    const zoomtest::Picture classic1 = one.shoot();
    one.renderer.set_world_art(&cut_pack);
    check(!same_pixels(one.shoot(), classic1) && one.renderer.world_art_textures() == 1, "a cut 2x sheet does not stop a 1x output from drawing its own");

    // a pack that was never loaded is none
    world_art::Pack empty;
    one.renderer.set_world_art(&empty);
    check(one.renderer.world_art() == nullptr && same_pixels(one.shoot(), classic1), "a pack that is not loaded is the original's sprites");
}

void test_fog(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("fog", "the stipple of a shrouded cell is the same pixels with either art; the cells in the open have the new ground");
    zoomtest::PixelRig rig(arc, "TREASURE", kW, kH);
    look_at_treasure(rig);
    const int32_t W = static_cast<int32_t>(rig.world.width), H = static_cast<int32_t>(rig.world.height);
    rig.world.fog_of_war_enabled = true;
    rig.world.fog_revealed.assign(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
    const int32_t c0 = static_cast<int32_t>(rig.cam().x) / 32, r0 = static_cast<int32_t>(rig.cam().y) / 32;
    for (int32_t r = r0 + 4; r < r0 + 9; ++r) {
        for (int32_t c = c0 + 4; c < c0 + 9; ++c) rig.world.fog_revealed[static_cast<size_t>(r) * static_cast<size_t>(W) + static_cast<size_t>(c)] = 1;
    }
    const zoomtest::Picture classic = rig.shoot();
    rig.renderer.set_world_art(&pack);
    const zoomtest::Picture art = rig.shoot();
    const LayoutRect view = rig.layout.view();
    // the original covers a shrouded cell with a stipple (dither0, every other pixel opaque black) over the ground, which shows through the rest: the stipple's pixels are the same with either art, and what lies
    // under the stipple is the ground, as in Classic
    const std::vector<uint8_t> stipple = arc.get_sprite(352).to_rgba32(arc.get_palette());
    const auto same_under_stipple = [&](int32_t x0, int32_t y0) {
        for (int32_t y = 0; y < 32; ++y) {
            for (int32_t x = 0; x < 32; ++x) {
                if (stipple[(static_cast<size_t>(y) * 32u + static_cast<size_t>(x)) * 4u + 3u] != 0 && std::memcmp(art.at(x0 + x, y0 + y), classic.at(x0 + x, y0 + y), 3) != 0) return false;
            }
        }
        return true;
    };
    size_t covered = 0, covered_same = 0, open = 0, open_changed = 0;
    for (int32_t r = r0; r < r0 + 14; ++r) {
        for (int32_t c = c0; c < c0 + 20; ++c) {
            const int32_t sx = view.x + c * 32 - static_cast<int32_t>(rig.cam().x), sy = view.y + r * 32 - static_cast<int32_t>(rig.cam().y);
            if (sx < view.x || sy < view.y || sx + 32 > view.x + view.w || sy + 32 > view.y + view.h || c + 1 >= W || r + 1 >= H) continue;
            const auto shrouded = [&](int32_t cc, int32_t rr) { return rig.world.fog_revealed[static_cast<size_t>(rr) * static_cast<size_t>(W) + static_cast<size_t>(cc)] == 0; };
            const bool whole = shrouded(c, r) && shrouded(c - 1, r) && shrouded(c + 1, r) && shrouded(c, r - 1) && shrouded(c, r + 1);
            const bool lit = !shrouded(c, r) && !shrouded(c - 1, r) && !shrouded(c + 1, r) && !shrouded(c, r - 1) && !shrouded(c, r + 1);
            const Block b{sx, sy, 32, 32};
            if (whole) {
                ++covered;
                covered_same += same_under_stipple(sx, sy) ? 1u : 0u;
            } else if (lit) {
                ++open;
                open_changed += same_block(art, classic, b) ? 0u : 1u;
            }
        }
    }
    check(covered >= 20 && covered_same == covered, "every one of the " + std::to_string(covered) + " cells that Fog of War covers wholly has the same stipple with the new art (" + std::to_string(covered_same) + ")");
    check(open >= 4 && open_changed >= 1, "...and the cells that are in the open have the new art: " + std::to_string(open_changed) + " of " + std::to_string(open) + " differ");
}

void test_hash(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("hash", "the state of a match is the same whether or not the new pictures are drawn");
    zoomtest::PixelRig rig(arc, "TREASURE", kW, kH);
    sim::SimulationEngine other;
    other.init(rig.level, 1337);
    rig.renderer.set_world_art(&pack);
    look_at_treasure(rig);
    for (int32_t t = 0; t < 60; ++t) {
        rig.engine.tick();
        other.tick();
        if (t % 6 == 0) {                                                       // (a frame of the new art now and then, from the live engine's own world)
            rig.world = rig.engine.get_world_state();
            rig.shoot();
        }
        if (t % 20 == 19) check(rig.engine.state_hash() == other.state_hash(), "the state hash after " + std::to_string(t + 1) + " ticks is the same");
    }
    check(rig.engine.state_hash() == other.state_hash(), "the state hash after 60 ticks of a match that was drawn with the new art is the one of a match that was not drawn at all");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The pictures of a batch (opt-in: ANTS_WORLD_ART_SHOTS=FOLDER ./build/tests/test_app/test_world_art): the real renderer's ground at 1x, 2x and 3x on TREASURE, ISLANDS and a players' map, the original's
// beside the new one, and the bridge's four stages, written as PNG files of at most 1000 pixels across. Nothing is judged.
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

bool write_crop(const std::string& path, const zoomtest::Picture& pic, int32_t x, int32_t y, int32_t w, int32_t h) {
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);
    for (int32_t j = 0; j < h; ++j) {
        for (int32_t i = 0; i < w; ++i) std::memcpy(&rgb[(static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i)) * 3u], pic.at(x + i, y + j), 3);
    }
    const std::string png = encode_png_rgb(rgb.data(), w, h);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

struct Scene {
    std::string name;
    std::string map;
    int32_t x, y, w, h;              // the crop, in world pixels (the same at 2x as at 1x; 3x shows a smaller one so that it stays under 1000 pixels)
    int32_t bridge_row;              // >= 0: four water cells at this row start at column `x / 32 + 1`: the stages of a bridge are laid on them
};

/// The first window of cols x rows cells of the map with the most different tiles in it (where the kinds of ground meet)
void busiest_window(zoomtest::PixelRig& rig, int32_t cols, int32_t rows, int32_t& best_c, int32_t& best_r) {
    int32_t best = -1;
    for (int32_t r = 0; r + rows <= static_cast<int32_t>(rig.tiles_h); r += 2) {
        for (int32_t c = 0; c + cols <= static_cast<int32_t>(rig.tiles_w); c += 2) {
            std::set<uint16_t> kinds;
            for (int32_t j = 0; j < rows; ++j) {
                for (int32_t i = 0; i < cols; ++i) kinds.insert(rig.engine.grid().get_cell(static_cast<uint32_t>(c + i), static_cast<uint32_t>(r + j)).terrain_id);
            }
            if (static_cast<int32_t>(kinds.size()) > best) {
                best = static_cast<int32_t>(kinds.size());
                best_c = c;
                best_r = r;
            }
        }
    }
}

void make_pictures(const assets::AssetArchive& arc, const world_art::Pack& pack, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    const std::string players_map = std::string(ANTS_SOURCE_DIR) + "/Community-Maps/TheGreatSea.lvl";
    std::vector<Scene> scenes = {
        {"treasure", "TREASURE", 320, 672, 480, 288, -1},
        {"islands", "ISLANDS", 704, 256, 480, 288, -1},
        {"players_map", players_map, 0, 0, 480, 288, -1},
        {"bridge", "ISLANDS", 0, 0, 160, 96, 0},
    };
    for (Scene& sc : scenes) {
        for (const int32_t os : {1, 2, 3}) {
            zoomtest::PixelRig rig(arc, sc.map, kW, kH, 0, os);
            if (!rig.ok) {
                std::fprintf(stderr, "picture: cannot start %s\n", sc.name.c_str());
                continue;
            }
            Scene s = sc;
            if (s.name == "players_map") busiest_window(rig, 15, 9, s.x, s.y), s.x *= 32, s.y *= 32;
            if (s.bridge_row == 0) {                                          // four water cells in a row
                bool found = false;
                for (int32_t r = 2; r < static_cast<int32_t>(rig.tiles_h) - 2 && !found; ++r) {
                    for (int32_t c = 3; c + 5 < static_cast<int32_t>(rig.tiles_w) && !found; ++c) {
                        bool all = true;
                        for (int32_t i = -1; i <= 4; ++i) {
                            for (int32_t j = -1; j <= 1; ++j) all = all && rig.engine.grid().get_cell(static_cast<uint32_t>(c + i), static_cast<uint32_t>(r + j)).terrain_type == sim::TERRAIN_WATER;
                        }
                        if (all) {
                            found = true;
                            s.x = c * 32 - 16;
                            s.y = r * 32 - 32;
                            s.bridge_row = r;
                            for (int32_t i = 0; i < 4; ++i) rig.engine.grid_mut().set_bridge_at(sim::TileCoord{c + i, r}, static_cast<uint8_t>(i + 1), 36000);
                        }
                    }
                }
                if (!found) std::fprintf(stderr, "picture: no water run on %s\n", s.map.c_str());
            }
            const int32_t w = os == 3 && s.w == 480 ? 320 : s.w;
            const int32_t h = os == 3 && s.h == 288 ? 192 : s.h;
            rig.world = rig.engine.get_world_state();
            rig.world.ants.clear();
            rig.look(s.x, s.y);
            const LayoutRect view = rig.layout.view();
            const auto cut = [&](const zoomtest::Picture& pic, const char* tag) {
                const std::string path = dir + "/" + sc.name + "_" + std::to_string(os) + "x_" + tag + ".png";
                if (!write_crop(path, pic, view.x * os, view.y * os, w * os, h * os)) std::fprintf(stderr, "picture: cannot write %s\n", path.c_str());
            };
            cut(rig.shoot(), "classic");
            rig.renderer.set_world_art(&pack);
            cut(rig.shoot(), "new");
        }
    }
    std::printf("pictures written to %s\n", dir.c_str());
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                  // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    assets::AssetArchive arc;
    world_art::Pack pack;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !pack.load(pack_folder())) {
        std::fprintf(stderr, "cannot open ants.chd or the world art: %s\n", pack.what().c_str());
        return 2;
    }
    if (const char* shots = std::getenv("ANTS_WORLD_ART_SHOTS")) {
        const zoomtest::QuietStdout quiet;
        make_pictures(arc, pack, shots);
        return 0;
    }
    test_manifest(arc);
    test_variants();
    test_draw(arc, pack);
    test_scale(arc, pack);
    test_classic(arc, pack);
    test_fallback(arc, pack);
    test_fog(arc, pack);
    test_hash(arc, pack);
    std::printf("\nworld art: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
