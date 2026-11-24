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
#include <cmath>
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

std::vector<std::string> g_copies;                              // the folders that pack_copy made (removed at the end of main)

bool read_text(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

/// A copy of the shipped pack in a folder of its own, with the manifest changed by `edit` and the sheets of the first page cut after their header where `cut` says (the first 80 bytes stay: the header is right, the body is not)
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
    for (const std::string prefix : {"sheet", "plants"}) {
        for (int32_t scale = 1; scale <= 3; ++scale) {
            const std::string name = prefix + "_" + std::to_string(scale) + "x.png";
            if (prefix != "sheet" || std::find(cut.begin(), cut.end(), scale) == cut.end()) {
                fs::copy_file(pack_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
                continue;
            }
            std::ifstream in(pack_folder() + "/" + name, std::ios::binary);
            std::vector<char> head(80);
            in.read(head.data(), static_cast<std::streamsize>(head.size()));
            std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
            out.write(head.data(), in.gcount());
        }
    }
    g_copies.push_back(dir.string());
    return dir.string();
}

std::string identity(const std::string& s) { return s; }

/// The manifest of the first page alone (the ground, the bridge and the effects of batches 1 to 3): the tests of the ground and of its scales, its textures and its fallbacks are about those sheets, and what
/// stands on the ground (the grass and the plants, test_world_art_plants.cpp) has pictures of its own on a page of its own, which a scene of these tests would draw as well
std::string first_page_only(const std::string& manifest) {
    std::istringstream in(manifest);
    std::string line, out;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string kind, first, second, third;
        words >> kind >> first >> second >> third;
        if (kind == "sheet" && first != "0") continue;                      // sheet <page> <scale> ...
        if (kind == "sprite" && third != "0") continue;                     // sprite <number> <variant> <page> ...
        out += line + "\n";
    }
    return out;
}

/// The world art of the batch of the ground alone: the looks of big mud fields and the transitions are made over frames and have their own tests (test_world_art_mud.cpp, test_world_art_edges.cpp)
void use_art(Renderer& renderer, const world_art::Pack& pack) {
    renderer.set_world_art(&pack);
    renderer.set_dense_mud(false);
    renderer.set_transitions(false);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [manifest]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// A tiny manifest: the header, the three sheet rows of page 0 and the given sprite rows
std::string tiny(const std::string& sprites, const std::string& sheets = "sheet 0 1 a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\n") {
    return "ants-world-art 2\n" + sheets + sprites;
}

void test_manifest(const assets::AssetArchive& arc) {
    group("manifest", "the shipped manifest and its sheets, and every way to break a manifest");
    world_art::Pack pack;
    check(pack.load(pack_folder()) && pack.loaded(), "the shipped pack is read: " + pack.what());
    size_t on_first_page = 0;
    for (const world_art::Picture& pic : pack.pictures()) on_first_page += pic.page == world_art::kGroundPage ? 1u : 0u;
    check(pack.page_count() == 2 && on_first_page == 1020 && pack.pictures().size() == 1066, "the pack has two pages: 1,020 pictures of the ground, the bridge and the effects (the plain ones and their variants, the bigger bubbles, the bombs, the explosion and the fire wall) and 46 of the grass and the plants: " + std::to_string(on_first_page) + " and " + std::to_string(pack.pictures().size() - on_first_page));
    size_t effects = 0;
    for (uint32_t sprite = 298; sprite <= 315; ++sprite) effects += pack.find(sprite, 0) != nullptr && pack.find(sprite, 0)->number == sprite ? 1u : 0u;
    for (uint32_t sprite = 306; sprite <= 315; ++sprite) effects += pack.find(30000 + sprite, 0) != nullptr && pack.find(30000 + sprite, 0)->number == 30000 + sprite ? 1u : 0u;
    for (uint32_t sprite = 316; sprite <= 320; ++sprite) effects += pack.find(50000 + sprite, 0) != nullptr && pack.find(50000 + sprite, 0)->number == 50000 + sprite ? 1u : 0u;
    check(effects == 33 && pack.find(316, 0) == nullptr, "the bombs (298 .. 305), the explosion (306 .. 315) and its light (30306 .. 30315) and the fire wall (50316 .. 50320) are there, and the fire's sprites are not offered as the original's own (316 .. 320 stay the original's: the Fire ant's frames use them)");
    size_t bigger = 0;
    for (uint32_t sprite = 641; sprite <= 650; ++sprite) {
        for (uint8_t v = 0; v < world_art::kVariants; ++v) bigger += (pack.find(10000 + sprite, v) != nullptr && pack.find(10000 + sprite, v)->variant == v ? 1u : 0u) + (pack.find(20000 + sprite, v) != nullptr && pack.find(20000 + sprite, v)->variant == v ? 1u : 0u);
    }
    check(bigger == 320, "the medium and the large bubble have a picture for each of the ten frames and sixteen variants: " + std::to_string(bigger));
    for (int32_t page = 0; page < pack.page_count(); ++page) {
        for (const int32_t scale : kArtScales) {
            const std::string what = "the sheet of page " + std::to_string(page) + " at scale " + std::to_string(scale);
            check(pack.sheet_width(page, scale) > 0 && pack.sheet_width(page, scale) <= 4096 && pack.sheet_height(page, scale) > 0 && pack.sheet_height(page, scale) <= 4096, what + " fits a texture of 4096");
            DecodedImage image;
            check(pack.sheet_pixels(page, scale, image) && image.width == pack.sheet_width(page, scale) && image.height == pack.sheet_height(page, scale), what + " decodes to the size the manifest states");
        }
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
    const std::string good = tiny("sprite 1 0 0 10 10 1 1 1 1 1 1\nsprite 2 0 0 10 10 20 1 40 1 60 1\n");
    check(p.parse(good) && p.pictures().size() == 2, "a small manifest is read");
    check(!p.parse(""), "an empty manifest is refused");
    check(!p.parse("ants-world-art 1\n") && !p.parse("ants-world-art 3\n"), "another version is refused");
    check(!p.parse("ants-hud-art 1\n"), "another header is refused");
    check(!p.parse(good.substr(0, good.find('\n') + 1)), "a manifest without sheets is refused");
    check(!p.parse(tiny("", "sheet 0 1 a.png 100 100\nsheet 0 3 c.png 300 300\n")), "a missing sheet row is refused");
    check(!p.parse(tiny("", "sheet 0 2 b.png 200 200\nsheet 0 1 a.png 100 100\nsheet 0 3 c.png 300 300\n")), "sheet rows out of order are refused");
    check(!p.parse(tiny("", "sheet 0 1 a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\nsheet 0 3 d.png 300 300\n")), "a sheet row twice is refused");
    check(!p.parse(tiny("", "sheet 0 1 ../a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\n")), "a sheet in another folder is refused");
    check(!p.parse(tiny("", "sheet 0 1 sub/a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\n")), "a sheet in a sub folder is refused");
    check(!p.parse(tiny("", "sheet 0 1 a.png 0 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\n")), "a sheet of no size is refused");
    check(!p.parse("sprite 1 0 0 10 10 1 1 1 1 1 1\n" + tiny("")), "a sprite before the header is refused");
    check(!p.parse("ants-world-art 2\nsprite 1 0 0 10 10 1 1 1 1 1 1\nsheet 0 1 a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\n"), "a sprite before the sheets is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 1 1 1\n")), "a sprite row that is short is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 1 1 1 1 x\n")), "a sprite row that holds a word is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 1 1 1 1 1\nsprite 1 0 0 10 10 20 1 40 1 60 1\n")), "a picture twice is refused");
    check(!p.parse(tiny("sprite 2 0 0 10 10 1 1 1 1 1 1\nsprite 1 0 0 10 10 20 1 40 1 60 1\n")), "pictures out of order are refused");
    check(!p.parse(tiny("sprite 1 16 0 10 10 1 1 1 1 1 1\n")), "a variant above 15 is refused");
    check(!p.parse(tiny("sprite 1 0 0 0 10 1 1 1 1 1 1\n")), "a picture of no width is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 0 1 1 1 1 1\n")), "a picture with no room for its ring (x = 0) is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 0 1 1 1 1\n")), "a picture with no room for its ring (y = 0) is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 90 1 1 1 1 1\n")), "a picture whose ring crosses the sheet's right side at 1x is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 1 1 190 1 1\n")), "a picture whose ring crosses the sheet's bottom at 2x is refused");
    check(p.parse(tiny("sprite 1 0 0 10 10 89 89 179 179 269 269\n")), "a picture that ends exactly at its ring against the side is read");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 1 1 1 1 1\nblob 3\n")), "a record of another kind is refused");
    check(!p.parse("ants-world-art 2\r\nsheet 0 1 a.png 100 100\r\n"), "CR LF line ends are refused");
    check(!p.parse("not a manifest") && !p.loaded() && p.pictures().empty(), "a manifest that was refused leaves a pack that is not loaded and holds nothing");
    world_art::Pack gone;
    check(!gone.load(pack_folder() + "/nothing here") && !gone.loaded(), "a folder without a manifest is not a pack");
    // pages: each has its own three sheets, a picture lies inside the sheets of ITS page, and a picture is on one page only
    const std::string pages = tiny("", "sheet 0 1 a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\nsheet 1 1 d.png 50 50\nsheet 1 2 e.png 100 100\nsheet 1 3 f.png 150 150\n");
    check(p.parse(pages + "sprite 1 0 0 10 10 1 1 1 1 1 1\nsprite 2 0 1 10 10 1 1 1 1 1 1\n") && p.page_count() == 2 && p.find(1, 0)->page == 0 && p.find(2, 0)->page == 1, "a manifest of two pages is read, each picture on its page");
    check(p.sheet_file(1, 2) == "e.png" && p.sheet_width(1, 3) == 150 && p.sheet_height(0, 3) == 300 && p.sheet_width(2, 1) == 0 && p.sheet_file(-1, 1).empty(), "...each page has its own sheets, and a page that is not there has none");
    check(p.parse(pages + "sprite 1 0 0 10 10 45 1 1 1 1 1\n") && !p.parse(pages + "sprite 1 0 1 10 10 45 1 1 1 1 1\n"), "a picture must lie inside the sheet of its own page: the same place is inside the sheet of page 0 and outside the narrower sheet of page 1");
    check(!p.parse(pages + "sprite 1 0 2 10 10 1 1 1 1 1 1\n"), "a picture on a page that the manifest does not have is refused");
    check(!p.parse(pages + "sprite 1 0 0 10 10 1 1 1 1 1 1\nsprite 1 0 1 10 10 1 1 1 1 1 1\n"), "a picture on two pages is refused");
    check(!p.parse(tiny("sprite 1 0 0 10 10 1 1 1 1 1 1\n", "sheet 0 1 a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\nsheet 1 1 d.png 50 50\n")), "a page with fewer than three sheet rows is refused");
    check(!p.parse(tiny("", "sheet 1 1 d.png 50 50\nsheet 1 2 e.png 100 100\nsheet 1 3 f.png 150 150\nsheet 0 1 a.png 100 100\nsheet 0 2 b.png 200 200\nsheet 0 3 c.png 300 300\n")), "pages out of order are refused");
    check(!p.parse(pages + "sprite 1 0 0 10 10 1 1 1 1 1 1\nsheet 2 1 g.png 10 10\n"), "a sheet row after a sprite is refused");
    std::string many = "";
    for (int32_t page = 0; page <= kArtMaxPages; ++page) {
        for (int32_t scale = 1; scale <= 3; ++scale) many += "sheet " + std::to_string(page) + " " + std::to_string(scale) + " p" + std::to_string(page * 3 + scale) + ".png 100 100\n";
    }
    check(!p.parse(tiny("", many)), "more pages than the game keeps (" + std::to_string(kArtMaxPages) + ") are refused");
    std::error_code ignored_error;
    const std::string cut = pack_copy("manifest_cut", identity, {2});
    world_art::Pack cut_pack;
    check(cut_pack.load(cut) && cut_pack.loaded(), "a sheet with a good header and no body passes the header look");
    DecodedImage image;
    check(!cut_pack.sheet_pixels(world_art::kGroundPage, 2, image) && image.width == 0, "...and cannot be decoded when it is needed");
    const std::string without_sheet = pack_copy("manifest_lacking", identity);                // the second page's 3x sheet is not in the folder: the pack is not whole, and it is the original's art from the start
    std::filesystem::remove(std::filesystem::path(without_sheet) / "plants_3x.png", ignored_error);
    world_art::Pack lacking_pack;
    check(!lacking_pack.load(without_sheet) && !lacking_pack.loaded(), "a pack whose second page lacks a sheet is refused at the start");
    const std::string wrong = pack_copy("manifest_wrong", [](const std::string& t) {
        std::string s = t;
        const size_t at = s.find("sheet 0 3 sheet_3x.png 3072");                     // the 3x sheet is stated one pixel narrower than the file: the file is not the picture the manifest says
        if (at != std::string::npos) s.replace(at, 27, "sheet 0 3 sheet_3x.png 3071");
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
    for (const int32_t s : kArtScales) {
        if (!pack.sheet_pixels(world_art::kGroundPage, s, sheets[s - 1])) {
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
        use_art(rig.renderer, pack);
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
    use_art(rig.renderer, pack);
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
        use_art(rig.renderer, pack);
        look_at_treasure(rig);
        rig.look(320, 672, 1.5f);
        rig.shoot();
        check(rig.renderer.world_art_sheets() == 1u && rig.renderer.world_art_scale_now() == 3, "a zoomed frame of an output of 3x makes the 1x sheet's texture only");
        rig.look(320, 672, 1.0f);
        rig.shoot();
        check(rig.renderer.world_art_sheets() == 5u, "...and the frame at the zoom 1 the 3x sheet's");
    }
    {
        // zoomed out, the pass's target is larger than the canvas and SDL measures that as the output: the pass must still take the 1x sheet
        zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, 1);
        use_art(rig.renderer, pack);
        rig.look(320, 672, 0.5f);
        rig.shoot();
        check(rig.renderer.world_art_sheets() == 1u, "a frame zoomed out to 0.5 draws from the 1x sheet only");
    }
    {
        zoomtest::PixelRig rig(arc, "TREASURE", kW, kH, 0, 3);
        const zoomtest::Picture classic = [&]() { look_at_treasure(rig); return rig.shoot(); }();
        use_art(rig.renderer, pack);
        rig.renderer.set_art_texture_limit(2600);                                // the 3x sheet (3072 x 3743) is too big, the 2x one (2048 x 2518) is not
        const zoomtest::Picture two = rig.shoot();
        check(rig.renderer.world_art_sheets() == 2u && !same_pixels(two, classic), "a device that takes no texture of 3072 gets the 2x sheet at the 3x output");
        rig.renderer.set_world_art(nullptr);
        use_art(rig.renderer, pack);
        rig.renderer.set_art_texture_limit(1000);                                // not even the 1x sheet (1024 x 1327)
        const zoomtest::Picture none = rig.shoot();
        check(rig.renderer.world_art_textures() == 0 && same_pixels(none, classic), "a device that takes no sheet at all draws the original's frame");
        rig.renderer.set_art_texture_limit(0);
        rig.renderer.set_world_art(nullptr);
        use_art(rig.renderer, pack);
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
        use_art(rig.renderer, pack);
        const zoomtest::Picture art = rig.shoot();
        check(!same_pixels(art, classic), "the art changes the frame at " + std::to_string(os) + "x");
        rig.renderer.set_world_art(nullptr);
        check(rig.renderer.world_art() == nullptr && rig.renderer.world_art_textures() == 0, "taking the art away drops its textures");
        check(same_pixels(rig.shoot(), classic), "the frame at " + std::to_string(os) + "x after the art was taken away is the original's, byte for byte");
        rig.look(320, 672, 0.5f);
        const zoomtest::Picture small_classic = rig.shoot();
        use_art(rig.renderer, pack);
        rig.look(320, 672, 0.5f);
        check(!same_pixels(rig.shoot(), small_classic), "a frame zoomed out has the art too");
    }
    // the picture of a whole map (the preview, the thumbnail) is the original's look
    zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH);
    WorldImage plain_image, art_image;
    std::string why;
    check(rig.renderer.render_world_image(rig.level, rig.world, rig.engine.grid(), plain_image, &why), "the picture of a map is made: " + why);
    use_art(rig.renderer, pack);
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
    const std::string wide = pack_copy("wide", [](const std::string& manifest) {
        std::istringstream in(first_page_only(manifest));
        std::string line, out;
        while (std::getline(in, line)) {
            if (line.rfind("sprite ", 0) == 0) {
                std::istringstream words(line);
                std::string word, number, variant, page;
                int w = 0, h = 0;
                words >> word >> number >> variant >> page >> w >> h;
                std::string rest;
                std::getline(words, rest);
                line = "sprite " + number + " " + variant + " " + page + " " + std::to_string(w + 1) + " " + std::to_string(h) + rest;
            }
            out += line + "\n";
        }
        return out;
    });
    world_art::Pack wide_pack;
    check(wide_pack.load(wide) && wide_pack.loaded(), "a pack whose pictures are all one pixel too wide is read");
    use_art(rig.renderer, wide_pack);
    check(same_pixels(rig.shoot(), classic), "...and draws the original's sprites, for none fits");

    // a sheet that is cut after its header: the manifest passes, the texture cannot be made; the output gets the next smaller sheet (the 1x one), and a pack with no sheet that can be had is the original's frame
    world_art::Pack cut_pack;
    check(cut_pack.load(pack_copy("cut2", first_page_only, {2})), "a pack with a cut 2x sheet is read");
    use_art(rig.renderer, cut_pack);
    const zoomtest::Picture smaller = rig.shoot();
    check(!same_pixels(smaller, classic) && rig.renderer.world_art_textures() == 1, "...and a 2x output draws from its 1x sheet: one texture, not the original's frame");
    check(same_pixels(rig.shoot(), smaller) && rig.renderer.world_art_textures() == 1, "...and again the same (a sheet that failed is not read at every frame)");
    world_art::Pack all_cut;
    check(all_cut.load(pack_copy("cut123", first_page_only, {1, 2, 3})), "a pack with all three sheets cut is read");
    use_art(rig.renderer, all_cut);
    check(same_pixels(rig.shoot(), classic) && rig.renderer.world_art_textures() == 0, "...and its frame is the original's, with no texture");
    check(same_pixels(rig.shoot(), classic), "...and again");

    // the cut sheet is not the one a 1x output needs: that output has its art
    zoomtest::PixelRig one(arc, "TREASURE", kW, kH, 0, 1);
    look_at_treasure(one);
    const zoomtest::Picture classic1 = one.shoot();
    use_art(one.renderer, cut_pack);
    check(!same_pixels(one.shoot(), classic1) && one.renderer.world_art_textures() == 1, "a cut 2x sheet does not stop a 1x output from drawing its own");

    // a pack that was never loaded is none
    world_art::Pack empty;
    use_art(one.renderer, empty);
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
    use_art(rig.renderer, pack);
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
    use_art(rig.renderer, pack);
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
            use_art(rig.renderer, pack);
            cut(rig.shoot(), "new");
        }
    }
    std::printf("pictures written to %s\n", dir.c_str());
}

/// The pictures of dense mud (ANTS_WORLD_ART_SHOTS=FOLDER ANTS_WORLD_ART_SET=mud): the world art with and without the looks of big mud fields, on a big field of a players' map, on GAUNTLET's pool, and on
/// TREASURE's crossing, and the bubbles of the big field at several moments
void make_mud_pictures(const assets::AssetArchive& arc, const world_art::Pack& pack, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    struct MudScene {
        std::string name, map;
        int32_t w, h;                                // the crop in world pixels at 1x and 2x (3x shows 2/3 of it)
        bool deepest;                                // centered on the deepest cell (else the crop is at x, y)
        int32_t x, y;
        bool moments;                                // the bubbles at several moments (3x only)
    };
    const std::vector<MudScene> scenes = {
        {"mudwars", std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Mud Wars.lvl", 480, 288, true, 0, 0, true},
        {"gauntlet", "GAUNTLET", 480, 288, true, 0, 0, false},
        {"treasure_crossing", "TREASURE", 480, 288, true, 0, 0, false},
    };
    for (const MudScene& sc : scenes) {
        for (const int32_t os : {1, 2, 3}) {
            zoomtest::PixelRig rig(arc, sc.map, kW, kH, 0, os);
            if (!rig.ok) {
                std::fprintf(stderr, "picture: cannot start %s\n", sc.name.c_str());
                continue;
            }
            rig.renderer.set_world_art(&pack);
            rig.renderer.set_dense_mud_budget_ms(-1);
            const int32_t w = os == 3 ? sc.w * 2 / 3 : sc.w, h = os == 3 ? sc.h * 2 / 3 : sc.h;
            int32_t cx = sc.x / 32, cy = sc.y / 32;
            if (sc.deepest) {
                double best = -1.0, best_away = 1e9;                 // (the deepest; of equally deep ones the nearest to the middle of the map)
                for (int32_t r = 0; r < static_cast<int32_t>(rig.tiles_h); ++r) {
                    for (int32_t c = 0; c < static_cast<int32_t>(rig.tiles_w); ++c) {
                        const double d = rig.renderer.mud_depth_at_cell(c, r);
                        const double away = std::hypot(c - rig.tiles_w / 2.0, r - rig.tiles_h / 2.0);
                        if (d > best + 1e-6 || (d > best - 1e-6 && away < best_away)) best = d, best_away = away, cx = c, cy = r;
                    }
                }
                std::printf("%s: deepest mud %.2f tiles at cell (%d, %d)\n", sc.name.c_str(), best, cx, cy);
            }
            rig.world = rig.engine.get_world_state();
            rig.world.ants.clear();
            rig.look(cx * 32 + 16 - sc.w / 2, cy * 32 + 16 - sc.h / 2);
            const LayoutRect view = rig.layout.view();
            const auto cut = [&](const zoomtest::Picture& pic, const std::string& tag) {
                const std::string path = dir + "/" + sc.name + "_" + std::to_string(os) + "x_" + tag + ".png";
                if (!write_crop(path, pic, view.x * os, view.y * os, w * os, h * os)) std::fprintf(stderr, "picture: cannot write %s\n", path.c_str());
            };
            rig.renderer.set_dense_mud(false);
            cut(rig.shoot(), "without");
            rig.renderer.set_dense_mud(true);
            cut(rig.shoot(), "dense");
            if (sc.moments && os == 3) {
                for (const uint32_t t : {1000u, 1250u, 1320u, 1420u, 1480u}) {
                    rig.renderer.pin_animation_clock(t);
                    cut(rig.shoot(), "dense_t" + std::to_string(t));
                }
            }
        }
    }
    std::printf("pictures written to %s\n", dir.c_str());
}

/// The pictures of the automatic transitions (ANTS_WORLD_ART_SHOTS=FOLDER ANTS_WORLD_ART_SET=edges): players' maps whose plain tiles meet in a hard line, at 1x, 2x and 3x, with and without the transitions
void make_edge_pictures(const assets::AssetArchive& arc, const world_art::Pack& pack, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    const std::vector<std::pair<std::string, std::string>> maps = {
        {"greatsea_water_gravel", "TheGreatSea.lvl"}, {"magnetic_water_gravel_dirt", "MaGnEtiC iSLaNd.lvl"}, {"coaster_water_dirt", "A_Coaster_Mix_Turnament_Map.lvl"}, {"arrowhead_water_slate", "ArrowHead.lvl"},
        {"bombraces_slate_gravel", "Bomb Races.lvl"}, {"chess_mud_gravel_grid", "CHESS.lvl"}, {"thegrid_water_mud_grid", "TheGrid.LVL"}, {"ants1998_dirt_slate_grid", "aNtS_oF_1998.lvl"},
    };
    for (const auto& entry : maps) {
        for (const int32_t os : {1, 2, 3}) {
            zoomtest::PixelRig rig(arc, std::string(ANTS_SOURCE_DIR) + "/Community-Maps/" + entry.second, kW, kH, 0, os);
            if (!rig.ok) {
                std::fprintf(stderr, "picture: cannot start %s\n", entry.second.c_str());
                continue;
            }
            rig.renderer.set_world_art(&pack);
            rig.renderer.set_dense_mud(false);
            rig.renderer.set_dense_mud_budget_ms(-1);
            // the window of 15 x 9 cells with the most transitions, not at the map's rim (a join there has nothing outside the picture)
            const int32_t cols = os == 3 ? 10 : 15, rows = os == 3 ? 6 : 9;
            int32_t best = -1, bx = 0, by = 0;
            for (int32_t y = 0; y + rows <= static_cast<int32_t>(rig.tiles_h); ++y) {
                for (int32_t x = 0; x + cols <= static_cast<int32_t>(rig.tiles_w); ++x) {
                    int32_t count = 0;
                    for (int32_t j = 0; j < rows; ++j) {
                        for (int32_t i = 0; i < cols; ++i) count += rig.renderer.cell_has_transition(x + i, y + j) ? 1 : 0;
                    }
                    if (count > best) best = count, bx = x, by = y;
                }
            }
            rig.world = rig.engine.get_world_state();
            rig.look(bx * 32, by * 32);
            const LayoutRect view = rig.layout.view();
            const auto cut = [&](const zoomtest::Picture& pic, const char* tag) {
                const std::string path = dir + "/" + entry.first + "_" + std::to_string(os) + "x_" + tag + ".png";
                if (!write_crop(path, pic, view.x * os, view.y * os, cols * 32 * os, rows * 32 * os)) std::fprintf(stderr, "picture: cannot write %s\n", path.c_str());
            };
            rig.renderer.set_transitions(false);
            cut(rig.shoot(), "without");
            rig.renderer.set_transitions(true);
            cut(rig.shoot(), "with");
            std::printf("%s at %dx: window (%d, %d), %d cells with a transition\n", entry.first.c_str(), os, bx, by, best);
        }
    }
    std::printf("pictures written to %s\n", dir.c_str());
}

/// An ant standing still at a place of the world (the idle clip of its kind, facing the same way as the ants of zoomtest::populate)
sim::AntSnapshot standing_ant(const assets::AssetArchive& arc, uint32_t id, uint8_t player, int type, int32_t px, int32_t py) {
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = player;
    a.type = static_cast<sim::AntType>(type);
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    a.hp = 10;
    a.max_hp = 10;
    a.state = sim::UnitState::Idle;
    const int32_t clip = zoomtest::idle_clip(arc, type, 3);
    a.loco_clip = clip < 0 ? uint16_t{0x7FFE} : static_cast<uint16_t>(clip);
    a.loco_frame = 0;
    a.loco_mirrored = false;
    a.loco_left_ms = 100;
    return a;
}

/// The pictures of the fire wall, the bombs and the explosion (ANTS_WORLD_ART_SHOTS=FOLDER ANTS_WORLD_ART_SET=effects): on gravel, on mud and beside water, at 1x, 2x and 3x, the original's beside the new one
void make_effect_pictures(const assets::AssetArchive& arc, const world_art::Pack& pack, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    struct EffectScene {
        std::string name, map;
        int32_t col, row;
    };
    const std::vector<EffectScene> scenes = {{"gravel", "ISLANDS", 23, 26}, {"mud", std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Mud Wars.lvl", 8, 14}, {"water", "ISLANDS", 7, 21}};
    for (const EffectScene& sc : scenes) {
        for (const int32_t os : {1, 2, 3}) {
            zoomtest::PixelRig rig(arc, sc.map, kW, kH, 0, os);
            if (!rig.ok) {
                std::fprintf(stderr, "picture: cannot start %s\n", sc.name.c_str());
                continue;
            }
            rig.renderer.set_dense_mud_budget_ms(-1);
            const int32_t x = sc.col * 32, y = sc.row * 32;
            rig.look(x - 48, y - 40);
            const LayoutRect view = rig.layout.view();
            const int32_t w = 160, h = 112;
            const auto cut = [&](const zoomtest::Picture& pic, const std::string& what, const char* tag) {
                const std::string path = dir + "/" + sc.name + "_" + what + "_" + std::to_string(os) + "x_" + tag + ".png";
                if (!write_crop(path, pic, view.x * os, view.y * os, w * os, h * os)) std::fprintf(stderr, "picture: cannot write %s\n", path.c_str());
            };
            const auto both = [&](const std::string& what, const std::function<void()>& set_up) {
                rig.renderer.set_world_art(nullptr);
                set_up();
                cut(rig.shoot(), what, "classic");
                rig.renderer.set_world_art(&pack);
                rig.renderer.set_dense_mud(false);
                cut(rig.shoot(), what, "new");
            };
            // fire walls, an ant standing in the first of them and one beside the last
            rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(sc.col), static_cast<uint32_t>(sc.row), 0);
            rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(sc.col + 2), static_cast<uint32_t>(sc.row), 0);
            both("fire", [&]() {
                rig.world = rig.engine.get_world_state();
                rig.world.ants.clear();
                rig.world.ants.push_back(standing_ant(arc, 9001, 0, 2, x + 16, y + 22));
                rig.world.ants.push_back(standing_ant(arc, 9002, 1, 0, x + 80, y + 54));
            });
            rig.engine.grid_mut().clear_firewall(static_cast<uint32_t>(sc.col), static_cast<uint32_t>(sc.row));
            rig.engine.grid_mut().clear_firewall(static_cast<uint32_t>(sc.col + 2), static_cast<uint32_t>(sc.row));
            // the four teams' bombs
            for (uint32_t t = 0; t < 4; ++t) rig.engine.grid_mut().place_bomb(static_cast<uint32_t>(sc.col) + t, static_cast<uint32_t>(sc.row), static_cast<uint8_t>(t));
            both("bombs", [&]() {
                rig.world = rig.engine.get_world_state();
                rig.world.ants.clear();
            });
            for (uint32_t t = 0; t < 4; ++t) rig.engine.grid_mut().clear_bomb(static_cast<uint32_t>(sc.col) + t, static_cast<uint32_t>(sc.row));
            if (sc.name == "water") continue;
            // the explosion at three moments of its 680 ms, an ant beside it
            for (const uint32_t ms : {90u, 250u, 410u}) {
                both("explosion_" + std::to_string(ms), [&]() {
                    rig.world = rig.engine.get_world_state();
                    rig.world.ants.clear();
                    rig.world.ants.push_back(standing_ant(arc, 9003, 0, 2, x + 40, y + 40));
                    rig.world.effects.clear();
                    sim::VisualEffect e;
                    e.anim_name = "bombex";
                    e.px = x;
                    e.py = y;
                    e.elapsed_ms = ms;
                    e.duration_ms = 680;
                    e.frame = static_cast<uint16_t>(ms / 50u);
                    e.total_frames = 14;
                    e.y_key = y;
                    rig.world.effects.push_back(e);
                });
            }
        }
    }
    std::printf("pictures written to %s\n", dir.c_str());
}

/// The pictures of the grass and the plants (ANTS_WORLD_ART_SHOTS=FOLDER ANTS_WORLD_ART_SET=plants): the busiest view of each of the original's maps (the most different kinds of plant in a window of 15 x 9
/// cells), the whole new look of the game (ground, transitions, plants) beside Classic, at 1x, 2x and 3x, with the animated flower at the moment of its first frame
void make_plant_pictures(const assets::AssetArchive& arc, const world_art::Pack& pack, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    std::set<uint32_t> plant_anims;
    for (uint32_t id = 0; id < arc.animation_count(); ++id) {
        const std::string& name = arc.get_animation(id).name;
        for (const char* prefix : {"grass", "clover", "flower", "dflower", "leaf", "stick", "mush", "plant", "ivy"}) {
            if (name.rfind(prefix, 0) == 0) plant_anims.insert(id);
        }
    }
    for (const char* map : {"TREASURE", "ISLANDS", "GAUNTLET", "MEDIUM", "SMALL"}) {
        for (const int32_t os : {1, 2, 3}) {
            zoomtest::PixelRig rig(arc, map, kW, kH, 0, os);
            if (!rig.ok) {
                std::fprintf(stderr, "picture: cannot start %s\n", map);
                continue;
            }
            // the window with the most different plants (then the most plants): the plants of the object list and the grass of the second layer
            std::vector<std::pair<uint32_t, std::pair<int32_t, int32_t>>> items;
            for (const auto& spawn : rig.level.anthill_spawns) {
                if (spawn.team_id == 255 && plant_anims.count(spawn.tile_id) != 0) items.push_back({spawn.tile_id, {spawn.x, spawn.y}});
            }
            for (uint32_t y = 0; y < rig.level.height(); ++y) {
                for (uint32_t x = 0; x < rig.level.width(); ++x) {
                    const uint16_t id = rig.level.get_cell_layer2(x, y).tile_index;
                    if (id != assets::LVL_EMPTY_TILE && plant_anims.count(id) != 0) items.push_back({id, {static_cast<int32_t>(x), static_cast<int32_t>(y)}});
                }
            }
            int32_t best_c = 0, best_r = 0, best_score = -1;
            for (int32_t r = 0; r + 9 <= static_cast<int32_t>(rig.tiles_h); r += 2) {
                for (int32_t c = 0; c + 15 <= static_cast<int32_t>(rig.tiles_w); c += 2) {
                    std::set<uint32_t> kinds;
                    int32_t count = 0;
                    for (const auto& item : items) {
                        if (item.second.first >= c && item.second.first < c + 15 && item.second.second >= r && item.second.second < r + 9) {
                            kinds.insert(item.first);
                            ++count;
                        }
                    }
                    const int32_t score = static_cast<int32_t>(kinds.size()) * 100 + std::min(count, 99);
                    if (score > best_score) {
                        best_score = score;
                        best_c = c;
                        best_r = r;
                    }
                }
            }
            const int32_t w = os == 3 ? 320 : 480, h = os == 3 ? 192 : 288;
            rig.world = rig.engine.get_world_state();
            rig.world.ants.clear();
            rig.look(best_c * 32, best_r * 32);
            const LayoutRect view = rig.layout.view();
            const auto cut = [&](const zoomtest::Picture& pic, const char* tag) {
                const std::string path = dir + "/plants_" + map + "_" + std::to_string(os) + "x_" + tag + ".png";
                if (!write_crop(path, pic, view.x * os, view.y * os, w * os, h * os)) std::fprintf(stderr, "picture: cannot write %s\n", path.c_str());
            };
            rig.renderer.set_dense_mud_budget_ms(-1);
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
    world_art::Pack ground;                                      // the first page alone (first_page_only): what the tests of the ground draw from
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !pack.load(pack_folder()) || !ground.load(pack_copy("ground", first_page_only))) {
        std::fprintf(stderr, "cannot open ants.chd or the world art: %s %s\n", pack.what().c_str(), ground.what().c_str());
        return 2;
    }
    if (const char* shots = std::getenv("ANTS_WORLD_ART_SHOTS")) {
        const zoomtest::QuietStdout quiet;
        const char* set = std::getenv("ANTS_WORLD_ART_SET");
        if (set != nullptr && std::string(set) == "mud") make_mud_pictures(arc, ground, shots);
        else if (set != nullptr && std::string(set) == "edges") make_edge_pictures(arc, ground, shots);
        else if (set != nullptr && std::string(set) == "effects") make_effect_pictures(arc, ground, shots);
        else if (set != nullptr && std::string(set) == "plants") make_plant_pictures(arc, pack, shots);
        else make_pictures(arc, ground, shots);
        return 0;
    }
    test_manifest(arc);
    test_variants();
    test_draw(arc, ground);
    test_scale(arc, ground);
    test_classic(arc, ground);
    test_fallback(arc, ground);
    test_fog(arc, ground);
    test_hash(arc, ground);
    std::error_code ignored;
    for (const std::string& dir : g_copies) std::filesystem::remove_all(dir, ignored);
    std::printf("\nworld art: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
